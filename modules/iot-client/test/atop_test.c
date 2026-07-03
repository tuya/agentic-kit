#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "atop.h"
#include "iot_client.h"
#include "iot_config_defaults.h"

#define MOCK_HOST "127.0.0.1"
#define MOCK_PORT 8443

#define TEST_UUID    "uuid_ci_test_12345678"
#define TEST_AUTHKEY "ci_authkey_1234567890abcdef"
#define TEST_TOKEN   "ci_test_token"
#define TEST_SW_VER  "1.0.0"
#define TEST_PK      "ci_test_product_key"
#define TEST_PV      "2.0"
#define TEST_BV      "1.0"

#define TEST_DEVID   "ci_device_test_001"
#define TEST_SEC_KEY "1234567890abcdef"

static pid_t mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;

static char *load_file(const pal_t *pal, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = pal->malloc(len + 1);
    if (buf) {
        size_t read_len = fread(buf, 1, len, f);
        if (read_len != (size_t)len) {
            pal->free(buf);
            fclose(f);
            return NULL;
        }
        buf[len] = '\0';
    }
    fclose(f);
    return buf;
}

#define RUN_TEST(fn)                                       \
    do {                                                   \
        tests_run++;                                       \
        printf("\n--- [%d] %s ---\n", tests_run, #fn);     \
        if ((fn)() == 0) {                                 \
            tests_passed++;                                \
            printf("  PASS\n");                            \
        } else {                                           \
            printf("  FAIL\n");                            \
        }                                                  \
    } while (0)

/* ---------- Mock server lifecycle ---------- */

/* Wait until MOCK_HOST:port accepts a TCP connection, up to timeout_ms. The mock
 * is a fork+exec'd Python server; a blind sleep races its startup on a loaded CI
 * box -- and now that tcp_connect is time-bounded, the first real connect to a
 * not-yet-ready mock times out and fails instead of blocking until ready. A
 * non-blocking connect probe waits until the port is genuinely connectable. */
static int wait_for_port(uint16_t port, int timeout_ms)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, MOCK_HOST, &addr.sin_addr);

    const int step_ms = 50;
    for (int waited = 0; waited <= timeout_ms; waited += step_ms) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
            if (rc == 0) { close(fd); return 0; }
            if (rc < 0 && errno == EINPROGRESS) {
                fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
                struct timeval tv = { .tv_sec = 0, .tv_usec = 300 * 1000 };
                if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
                    int err = 0; socklen_t len = sizeof(err);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0) { close(fd); return 0; }
                }
            }
            close(fd);
        }
        usleep(step_ms * 1000);
    }
    return -1;
}

static int start_mock_server(void)
{
    mock_pid = fork();
    if (mock_pid == 0) {
        setenv("ATOP_MOCK_USE_SSL", "1", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MOCK_SCRIPT_PATH, NULL);
        perror("execlp failed");
        _exit(1);
    }
    if (mock_pid < 0) {
        perror("fork");
        return -1;
    }
    printf("Mock server started (pid %d, port %u), waiting for ready...\n", mock_pid, MOCK_PORT);
    if (wait_for_port(MOCK_PORT, 15000) != 0) {
        fprintf(stderr, "ATOP mock (%u) never became connectable\n", MOCK_PORT);
        return -1;
    }
    return OPRT_OK;
}

static void stop_mock_server(void)
{
    if (mock_pid > 0) {
        printf("Stopping mock server (pid %d)...\n", mock_pid);
        kill(mock_pid, SIGTERM);
        waitpid(mock_pid, NULL, 0);
        mock_pid = -1;
    }
}

/* ---------- Test: device activation ---------- */

static int test_activate(void)
{
    const pal_t *pal = get_default_pal();
    activite_request_t req = {
        .token       = TEST_TOKEN,
        .sw_ver      = TEST_SW_VER,
        .product_key = TEST_PK,
        .pv          = TEST_PV,
        .bv          = TEST_BV,
        .authkey     = TEST_AUTHKEY,
        .uuid        = TEST_UUID,
        .sdk_version = SDK_VERSION,
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert = g_cacert,
    };
    activite_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rt = atop_activate_request(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_activate_request failed: %d\n", rt);
        return -1;
    }

    if (resp.devid == NULL || strlen(resp.devid) == 0) {
        printf("  activate response missing devid\n");
        atop_activate_response_free(pal, &resp);
        return -1;
    }
    if (resp.secret_key == NULL || strlen(resp.secret_key) == 0) {
        printf("  activate response missing secret_key\n");
        atop_activate_response_free(pal, &resp);
        return -1;
    }
    if (resp.local_key == NULL || strlen(resp.local_key) == 0) {
        printf("  activate response missing local_key\n");
        atop_activate_response_free(pal, &resp);
        return -1;
    }
    printf("  devid      : %s\n", resp.devid);
    printf("  secret_key : %s\n", resp.secret_key ? resp.secret_key : "(null)");
    printf("  local_key  : %s\n", resp.local_key  ? resp.local_key  : "(null)");
    atop_activate_response_free(pal, &resp);
    return OPRT_OK;
}

/* ---------- Test: AI token ---------- */

static int test_ai_token(void)
{
    const pal_t *pal = get_default_pal();
    ai_token_request_t req = {
        .devid      = TEST_DEVID,
        .key        = TEST_SEC_KEY,
        .agent_code = NULL,
        .host       = MOCK_HOST,
        .port       = MOCK_PORT,
        .cacert = g_cacert,
    };
    ai_token_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rt = atop_ai_token_get(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_ai_token_get failed: %d\n", rt);
        return -1;
    }

    if (resp.token == NULL || resp.token[0] == '\0') {
        printf("  token is empty\n");
        return -1;
    }
    printf("  token: %s\n", resp.token);
    pal->free(resp.token);
    return OPRT_OK;
}

/* ---------- Test: parameter validation ---------- */

static int test_activate_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_activate_request(pal, NULL, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

static int test_ai_token_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_ai_token_get(pal, NULL, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

/* ---------- Test: missing required parameters ---------- */

static int test_activate_missing_token(void)
{
    const pal_t *pal = get_default_pal();
    activite_request_t req = {
        .token       = "",  // Empty token
        .sw_ver      = TEST_SW_VER,
        .product_key = TEST_PK,
        .pv          = TEST_PV,
        .bv          = TEST_BV,
        .authkey     = TEST_AUTHKEY,
        .uuid        = TEST_UUID,
        .sdk_version = SDK_VERSION,
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert      = g_cacert,
    };
    activite_response_t resp = {0};

    int rt = atop_activate_request(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty token, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty token\n");
    return OPRT_OK;
}

static int test_activate_missing_uuid(void)
{
    const pal_t *pal = get_default_pal();
    activite_request_t req = {
        .token       = TEST_TOKEN,
        .sw_ver      = TEST_SW_VER,
        .product_key = TEST_PK,
        .pv          = TEST_PV,
        .bv          = TEST_BV,
        .authkey     = TEST_AUTHKEY,
        .uuid        = "",  // Empty uuid
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert      = g_cacert,
    };
    activite_response_t resp = {0};

    int rt = atop_activate_request(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty uuid, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty uuid\n");
    return OPRT_OK;
}

static int test_ai_token_missing_devid(void)
{
    const pal_t *pal = get_default_pal();
    ai_token_request_t req = {
        .devid      = "",  // Empty devid
        .key        = TEST_SEC_KEY,
        .agent_code = NULL,
        .host       = MOCK_HOST,
        .port       = MOCK_PORT,
        .cacert     = g_cacert,
    };
    ai_token_response_t resp = {0};

    int rt = atop_ai_token_get(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty devid, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty devid\n");
    return OPRT_OK;
}

static int test_ai_token_missing_key(void)
{
    const pal_t *pal = get_default_pal();
    ai_token_request_t req = {
        .devid      = TEST_DEVID,
        .key        = "",  // Empty key
        .agent_code = NULL,
        .host       = MOCK_HOST,
        .port       = MOCK_PORT,
        .cacert     = g_cacert,
    };
    ai_token_response_t resp = {0};

    int rt = atop_ai_token_get(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty key, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty key\n");
    return OPRT_OK;
}

/* ---------- Test: TLS certificate verification failure ---------- */

static const char *WRONG_CA_CERT =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkTCB+wIJAKHBfpegoBjZMA0GCSqGSIb3DQEBCwUAMBExDzANBgNVBAMMBnVu\n"
    "dXNlZDAeFw0yMzAxMDEwMDAwMDBaFw0zMzAxMDEwMDAwMDBaMBExDzANBgNVBAMM\n"
    "BnVudXNlZDBcMA0GCSqGSIb3DQEBAQUAA0sAMEgCQQC7o96FCEcJl6sdquBvzWJg\n"
    "J9m3vO1QqOPNPEtuV1Gb7rlqU1Vq7i7pKXLVfXi6vT9rQ5TSieqF8MEpJBnNlQu1\n"
    "AgMBAAGjUzBRMB0GA1UdDgQWBBQWzTnvkMp8dB7W5VpJfJMZr3S9ejAfBgNVHSME\n"
    "GDAWgBQWzTnvkMp8dB7W5VpJfJMZr3S9ejAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
    "SIb3DQEBCwUAA0EAHcKnbNz/L5bOCjY9dOGz+oDi4sNGj9xLD3kzxNviY9eUH6ne\n"
    "tAgG7r3pYFGuhQ+A1hHRNL8hNrlkk8L6EjVlJA==\n"
    "-----END CERTIFICATE-----\n";

static int test_activate_tls_cert_fail(void)
{
    const pal_t *pal = get_default_pal();
    activite_request_t req = {
        .token       = TEST_TOKEN,
        .sw_ver      = TEST_SW_VER,
        .product_key = TEST_PK,
        .pv          = TEST_PV,
        .bv          = TEST_BV,
        .authkey     = TEST_AUTHKEY,
        .uuid        = TEST_UUID,
        .sdk_version = SDK_VERSION,
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert      = WRONG_CA_CERT,  // Wrong CA certificate
    };
    activite_response_t resp = {0};

    int rt = atop_activate_request(pal, &req, &resp);
    if (rt == OPRT_OK) {
        printf("  expected TLS failure with wrong CA cert, but got success\n");
        return -1;
    }
    printf("  correctly failed with error %d (TLS cert verification)\n", rt);
    return OPRT_OK;
}

static int test_ai_token_tls_cert_fail(void)
{
    const pal_t *pal = get_default_pal();
    ai_token_request_t req = {
        .devid      = TEST_DEVID,
        .key        = TEST_SEC_KEY,
        .agent_code = NULL,
        .host       = MOCK_HOST,
        .port       = MOCK_PORT,
        .cacert     = WRONG_CA_CERT,  // Wrong CA certificate
    };
    ai_token_response_t resp = {0};

    int rt = atop_ai_token_get(pal, &req, &resp);
    if (rt == OPRT_OK) {
        printf("  expected TLS failure with wrong CA cert, but got success\n");
        return -1;
    }
    printf("  correctly failed with error %d (TLS cert verification)\n", rt);
    return OPRT_OK;
}

/* ---------- Test: connection failures ---------- */

static int test_activate_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    activite_request_t req = {
        .token       = TEST_TOKEN,
        .sw_ver      = TEST_SW_VER,
        .product_key = TEST_PK,
        .pv          = TEST_PV,
        .bv          = TEST_BV,
        .authkey     = TEST_AUTHKEY,
        .uuid        = TEST_UUID,
        .sdk_version = SDK_VERSION,
        .host        = MOCK_HOST,
        .port        = 19999,  // Wrong port, no server listening
        .cacert      = g_cacert,
    };
    activite_response_t resp = {0};

    int rt = atop_activate_request(pal, &req, &resp);
    if (rt == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", rt);
    return OPRT_OK;
}

static int test_ai_token_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    ai_token_request_t req = {
        .devid      = TEST_DEVID,
        .key        = TEST_SEC_KEY,
        .agent_code = NULL,
        .host       = MOCK_HOST,
        .port       = 19999,  // Wrong port, no server listening
        .cacert     = g_cacert,
    };
    ai_token_response_t resp = {0};

    int rt = atop_ai_token_get(pal, &req, &resp);
    if (rt == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", rt);
    return OPRT_OK;
}

/* ---------- Test: QR code info ---------- */

static int test_qrcode_info(void)
{
    const pal_t *pal = get_default_pal();
    qrcode_info_request_t req = {
        .uuid    = TEST_UUID,
        .authkey = TEST_AUTHKEY,
        .app_id  = "test_app",
        .type    = 0,
        .host    = MOCK_HOST,
        .port    = MOCK_PORT,
        .cacert  = g_cacert,
    };
    qrcode_info_response_t resp = {0};

    int rt = atop_qrcode_info_get(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_qrcode_info_get failed: %d\n", rt);
        return -1;
    }
    if (resp.short_url == NULL || resp.short_url[0] == '\0') {
        printf("  qrcode short_url is empty\n");
        return -1;
    }
    printf("  url: %s\n", resp.short_url);
    pal->free(resp.short_url);
    return OPRT_OK;
}

static int test_qrcode_info_null_params(void)
{
    int rt = iot_get_qrcode_info(NULL, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

static int test_qrcode_info_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    qrcode_info_request_t req = {
        .uuid    = TEST_UUID,
        .authkey = TEST_AUTHKEY,
        .app_id  = "",
        .type    = 0,
        .host    = MOCK_HOST,
        .port    = 19999,
        .cacert  = g_cacert,
    };
    qrcode_info_response_t resp = {0};

    int rt = atop_qrcode_info_get(pal, &req, &resp);
    if (rt == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", rt);
    return OPRT_OK;
}

/* ---------- Test: device meta save ---------- */

static int test_device_meta_save(void)
{
    const pal_t *pal = get_default_pal();
    device_meta_save_request_t req = {
        .devid       = TEST_DEVID,
        .key         = TEST_SEC_KEY,
        .sdk_version = "agentic-kit 0.1",
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert      = g_cacert,
    };
    device_meta_save_response_t resp = {0};

    int rt = atop_device_meta_save(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_device_meta_save failed: %d\n", rt);
        return -1;
    }
    if (!resp.success) {
        printf("  device meta save response success is false\n");
        return -1;
    }
    printf("  device meta save success\n");
    return OPRT_OK;
}

static int test_device_meta_save_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_device_meta_save(pal, NULL, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

static int test_device_meta_save_missing_meta(void)
{
    const pal_t *pal = get_default_pal();
    device_meta_save_request_t req = {
        .devid       = TEST_DEVID,
        .key         = TEST_SEC_KEY,
        .sdk_version = "",  // Empty sdk_version
        .host        = MOCK_HOST,
        .port        = MOCK_PORT,
        .cacert      = g_cacert,
    };
    device_meta_save_response_t resp = {0};

    int rt = atop_device_meta_save(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty sdk_version, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty sdk_version\n");
    return OPRT_OK;
}

/* ---------- main ---------- */

int main(void)
{
    printf("========== ATOP Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Failed to load CA certificate from %s\n",
                TEST_CONFIG_DIR "/root_cert.pem");
        return 1;
    }

    if (start_mock_server() != 0) {
        fprintf(stderr, "Failed to start mock server\n");
        return 1;
    }

    /* Parameter validation tests */
    RUN_TEST(test_activate_null_params);
    RUN_TEST(test_ai_token_null_params);
    RUN_TEST(test_qrcode_info_null_params);
    RUN_TEST(test_device_meta_save_null_params);
    RUN_TEST(test_activate_missing_token);
    RUN_TEST(test_activate_missing_uuid);
    RUN_TEST(test_ai_token_missing_devid);
    RUN_TEST(test_ai_token_missing_key);
    RUN_TEST(test_device_meta_save_missing_meta);
    /* TLS certificate verification failure tests */
    RUN_TEST(test_activate_tls_cert_fail);
    RUN_TEST(test_ai_token_tls_cert_fail);

    /* Connection failure tests */
    RUN_TEST(test_activate_connection_fail);
    RUN_TEST(test_ai_token_connection_fail);
    RUN_TEST(test_qrcode_info_connection_fail);

    /* Success tests */
    RUN_TEST(test_activate);
    RUN_TEST(test_ai_token);
    RUN_TEST(test_qrcode_info);
    RUN_TEST(test_device_meta_save);

    stop_mock_server();

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
