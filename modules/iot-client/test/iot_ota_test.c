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
        fprintf(stderr, "OTA mock (%u) never became connectable\n", MOCK_PORT);
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

/* ---------- Test: version update ---------- */

static int test_version_update(void)
{
    const pal_t *pal = get_default_pal();
    ota_version_update_request_t req = {
        .devid  = TEST_DEVID,
        .key    = TEST_SEC_KEY,
        .sw_ver = "1.2.3",
        .pv     = "2.3",
        .bv     = "2.0",
        .channel = 0,
        .host   = MOCK_HOST,
        .port   = MOCK_PORT,
        .cacert = g_cacert,
    };

    int rt = atop_version_update(pal, &req);
    if (rt != OPRT_OK) {
        printf("  atop_version_update failed: %d\n", rt);
        return -1;
    }
    printf("  version update success\n");
    return OPRT_OK;
}

static int test_version_update_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_version_update(pal, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

static int test_version_update_missing_sw_ver(void)
{
    const pal_t *pal = get_default_pal();
    ota_version_update_request_t req = {
        .devid  = TEST_DEVID,
        .key    = TEST_SEC_KEY,
        .sw_ver = "",
        .host   = MOCK_HOST,
        .port   = MOCK_PORT,
        .cacert = g_cacert,
    };

    int rt = atop_version_update(pal, &req);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty sw_ver, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty sw_ver\n");
    return OPRT_OK;
}

/* ---------- Test: upgrade get (with upgrade available) ---------- */

static int test_upgrade_get_with_upgrade(void)
{
    const pal_t *pal = get_default_pal();
    ota_upgrade_request_t req = {
        .devid   = TEST_DEVID,
        .key     = TEST_SEC_KEY,
        .channel = 0,
        .sw_ver  = "1.0.0",
        .host    = MOCK_HOST,
        .port    = MOCK_PORT,
        .cacert  = g_cacert,
    };
    ota_upgrade_response_t resp = {0};

    int rt = atop_upgrade_get(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_upgrade_get failed: %d\n", rt);
        return -1;
    }

    if (!resp.has_upgrade) {
        printf("  expected has_upgrade=true\n");
        atop_upgrade_get_response_free(pal, &resp);
        return -1;
    }
    if (resp.url == NULL || resp.url[0] == '\0') {
        printf("  url is empty\n");
        atop_upgrade_get_response_free(pal, &resp);
        return -1;
    }
    if (resp.version == NULL || resp.version[0] == '\0') {
        printf("  version is empty\n");
        atop_upgrade_get_response_free(pal, &resp);
        return -1;
    }
    if (resp.file_size <= 0) {
        printf("  file_size is invalid: %ld\n", resp.file_size);
        atop_upgrade_get_response_free(pal, &resp);
        return -1;
    }

    printf("  version: %s\n", resp.version);
    printf("  url: %s\n", resp.url);
    printf("  size: %ld\n", resp.file_size);
    printf("  md5: %s\n", resp.md5 ? resp.md5 : "(null)");
    printf("  hmac: %s\n", resp.hmac ? resp.hmac : "(null)");

    atop_upgrade_get_response_free(pal, &resp);
    return OPRT_OK;
}

/* ---------- Test: upgrade get (no upgrade for non-zero channel) ---------- */

static int test_upgrade_get_no_upgrade(void)
{
    const pal_t *pal = get_default_pal();
    ota_upgrade_request_t req = {
        .devid   = TEST_DEVID,
        .key     = TEST_SEC_KEY,
        .channel = 9,  /* mock returns empty result for non-zero channel */
        .host    = MOCK_HOST,
        .port    = MOCK_PORT,
        .cacert  = g_cacert,
    };
    ota_upgrade_response_t resp = {0};

    int rt = atop_upgrade_get(pal, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  atop_upgrade_get failed: %d\n", rt);
        return -1;
    }

    if (resp.has_upgrade) {
        printf("  expected has_upgrade=false\n");
        atop_upgrade_get_response_free(pal, &resp);
        return -1;
    }

    printf("  no upgrade available (correct)\n");
    atop_upgrade_get_response_free(pal, &resp);
    return OPRT_OK;
}

static int test_upgrade_get_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_upgrade_get(pal, NULL, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

static int test_upgrade_get_missing_devid(void)
{
    const pal_t *pal = get_default_pal();
    ota_upgrade_request_t req = {
        .devid   = "",
        .key     = TEST_SEC_KEY,
        .channel = 0,
        .host    = MOCK_HOST,
        .port    = MOCK_PORT,
        .cacert  = g_cacert,
    };
    ota_upgrade_response_t resp = {0};

    int rt = atop_upgrade_get(pal, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty devid, got %d\n", rt);
        return -1;
    }
    printf("  correctly rejected empty devid\n");
    return OPRT_OK;
}

/* ---------- Test: upgrade status update ---------- */

static int test_upgrade_status_update(void)
{
    const pal_t *pal = get_default_pal();
    iot_ota_status_t statuses[] = {
        OTA_STATUS_UPGRADING,
        OTA_STATUS_UPGRAD_FINI,
        OTA_STATUS_UPGRD_EXEC,
        OTA_STATUS_UPGRD_ABORT,
    };
    const char *names[] = { "UPGRADING", "FINISHED", "FAILED", "ABORTED" };

    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
        ota_status_update_request_t req = {
            .devid   = TEST_DEVID,
            .key     = TEST_SEC_KEY,
            .channel = 0,
            .status  = statuses[i],
            .host    = MOCK_HOST,
            .port    = MOCK_PORT,
            .cacert  = g_cacert,
        };

        int rt = atop_upgrade_status_update(pal, &req);
        if (rt != OPRT_OK) {
            printf("  status %s failed: %d\n", names[i], rt);
            return -1;
        }
        printf("  status %s: OK\n", names[i]);
    }
    return OPRT_OK;
}

static int test_upgrade_status_update_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int rt = atop_upgrade_status_update(pal, NULL);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", rt);
        return -1;
    }
    return OPRT_OK;
}

/* ---------- main ---------- */

int main(void)
{
    printf("========== OTA Test Suite ==========\n");

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
    RUN_TEST(test_version_update_null_params);
    RUN_TEST(test_version_update_missing_sw_ver);
    RUN_TEST(test_upgrade_get_null_params);
    RUN_TEST(test_upgrade_get_missing_devid);
    RUN_TEST(test_upgrade_status_update_null_params);

    /* Success tests */
    RUN_TEST(test_version_update);
    RUN_TEST(test_upgrade_get_with_upgrade);
    RUN_TEST(test_upgrade_get_no_upgrade);
    RUN_TEST(test_upgrade_status_update);

    stop_mock_server();

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
