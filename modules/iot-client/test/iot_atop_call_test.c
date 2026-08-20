/**
 * @file iot_atop_call_test.c
 * @brief Tests for the generic ATOP entry point (iot_atop_call).
 *
 * Covers argument guards, the credential requirement, body validation, result
 * pass-through and error pass-through. The envelope error contract this relies
 * on is tested in atop_test.c.
 *
 * The client is a stack iot_client_t with devid/secret_key/https_url set, so no
 * activation or MQTT connection is needed; https_url points the ATOP host
 * resolution at the mock server.
 */

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
#include <time.h>

#include "iot_atop.h"
#include "iot_client.h"
#include "iot_config_defaults.h"

#define MOCK_HOST "127.0.0.1"
#define MOCK_PORT 8443

/* Must match test/config/atop.conf (device_id / sec_key). */
#define TEST_DEVID   "ci_device_test_001"
#define TEST_SEC_KEY "1234567890abcdef"

static pid_t mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;
static iot_client_t g_client;

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

/* ---------- Mock server lifecycle (same probe as iot_ota_test) ---------- */

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
    return 0;
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

/* ---------- iot_atop_call: guards ---------- */

static int test_null_params(void)
{
    iot_atop_request_t req = { .api = "tuya.device.meta.save", .version = "1.0" };
    iot_atop_response_t resp = {0};

    if (iot_atop_call(NULL, &req, &resp) != OPRT_INVALID_PARAMETER ||
        iot_atop_call(&g_client, NULL, &resp) != OPRT_INVALID_PARAMETER ||
        iot_atop_call(&g_client, &req, NULL) != OPRT_INVALID_PARAMETER) {
        printf("  NULL guards failed\n");
        return -1;
    }
    iot_atop_response_free(&g_client, NULL);  /* must be a safe no-op */
    return 0;
}

static int test_api_and_version_required(void)
{
    iot_atop_response_t resp = {0};
    const iot_atop_request_t bad[] = {
        { .api = NULL,                       .version = "1.0" },
        { .api = "",                         .version = "1.0" },
        { .api = "tuya.device.meta.save",    .version = NULL  },
        { .api = "tuya.device.meta.save",    .version = ""    },
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int rt = iot_atop_call(&g_client, &bad[i], &resp);
        if (rt != OPRT_INVALID_PARAMETER) {
            printf("  case %zu returned %d, expected OPRT_INVALID_PARAMETER\n", i, rt);
            return -1;
        }
    }
    return 0;
}

static int test_requires_device_credentials(void)
{
    iot_client_t bare = g_client;
    bare.devid[0] = '\0';

    iot_atop_request_t req = { .api = "tuya.device.meta.save", .version = "1.0" };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&bare, &req, &resp);
    if (rt != OPRT_UNINITIALIZED) {
        printf("  returned %d, expected OPRT_UNINITIALIZED\n", rt);
        return -1;
    }

    bare = g_client;
    bare.secret_key[0] = '\0';
    rt = iot_atop_call(&bare, &req, &resp);
    if (rt != OPRT_UNINITIALIZED) {
        printf("  empty secret_key returned %d, expected OPRT_UNINITIALIZED\n", rt);
        return -1;
    }
    return 0;
}

/* A malformed body is caught locally, before spending an HTTPS round trip. */
static int test_body_must_be_json_object(void)
{
    iot_atop_response_t resp = {0};
    const char *bad_bodies[] = {
        "{not json",
        "[1,2,3]",      /* valid JSON, but an array is not a request body */
        "\"string\"",
        "42",
        "{}garbage",    /* trailing garbage after a complete value */
        "{\"t\":1}}",
    };

    for (size_t i = 0; i < sizeof(bad_bodies) / sizeof(bad_bodies[0]); i++) {
        iot_atop_request_t req = { .api = "tuya.device.meta.save",
                                   .version = "1.0",
                                   .data = bad_bodies[i] };
        int rt = iot_atop_call(&g_client, &req, &resp);
        if (rt != OPRT_INVALID_PARAMETER) {
            printf("  body %zu (%s) returned %d, expected OPRT_INVALID_PARAMETER\n",
                   i, bad_bodies[i], rt);
            iot_atop_response_free(&g_client, &resp);
            return -1;
        }
    }
    return 0;
}

/* ---------- iot_atop_call: round trips ---------- */

static int test_call_returns_result_json(void)
{
    char body[128];
    snprintf(body, sizeof(body),
             "{\"metas\":{\"smain_network_sdk_full_version\":\"test 0.1\"},\"t\":%u}",
             (unsigned)time(NULL));

    iot_atop_request_t req = { .api = "tuya.device.meta.save",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  returned %d (%s / %s), expected OPRT_OK\n",
               rt, resp.error_code, resp.error_msg);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.error_code[0] != '\0') {
        printf("  error_code should be empty on success, got \"%s\"\n", resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.result == NULL || strcmp(resp.result, "true") != 0) {
        printf("  result is \"%s\", expected \"true\"\n",
               resp.result ? resp.result : "(null)");
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.server_time <= 0) {
        printf("  server_time was not carried out of the envelope\n");
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    printf("  result=%s server_time=%d\n", resp.result, resp.server_time);
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* A non-scalar result comes back as its JSON text, unparsed -- that is the
 * contract: cJSON stays out of the public ABI. */
static int test_call_returns_json_array(void)
{
    char body[160];
    snprintf(body, sizeof(body),
             "{\"schemaId\":\"test_schema_id\",\"version\":\"\",\"t\":%u}",
             (unsigned)time(NULL));

    iot_atop_request_t req = { .api = "tuya.device.schema.newest.get",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_OK || resp.result == NULL) {
        printf("  returned %d, result=%s\n", rt, resp.result ? resp.result : "(null)");
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.result[0] != '[') {
        printf("  expected a JSON array, got: %.40s\n", resp.result);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    printf("  result=%.60s...\n", resp.result);
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* A NULL body means "{}" -- enough for interfaces that take no arguments. */
static int test_null_body_defaults_to_empty_object(void)
{
    iot_atop_request_t req = { .api = "tuya.device.schema.newest.get",
                               .version = "1.0",
                               .data = NULL };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  returned %d (%s), expected OPRT_OK\n", rt, resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* A {"result":null} envelope means "nothing to report" -- it must come back as
 * a NULL pointer, never as the four-character string "null". */
static int test_null_result_stays_null(void)
{
    char body[64];
    snprintf(body, sizeof(body), "{\"t\":%u}", (unsigned)time(NULL));

    iot_atop_request_t req = { .api = "tuya.test.result.null",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_OK) {
        printf("  returned %d (%s), expected OPRT_OK\n", rt, resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.result != NULL) {
        printf("  result is \"%s\", expected NULL for a JSON null\n", resp.result);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* GATEWAY_NOT_EXISTS historically mapped to OPRT_COMMUNICATION_ERROR, which the
 * documented return-code table classifies as retryable transport failure -- a
 * permanent-retry trap for a device that was deleted from the cloud. Pin the
 * uniform verdict so the special case cannot quietly come back. */
static int test_gateway_not_exists_is_business_error(void)
{
    char body[64];
    snprintf(body, sizeof(body), "{\"t\":%u}", (unsigned)time(NULL));

    iot_atop_request_t req = { .api = "tuya.test.gateway.gone",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_ATOP_BUSINESS_ERROR) {
        printf("  returned %d, expected OPRT_ATOP_BUSINESS_ERROR\n", rt);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (strcmp(resp.error_code, "GATEWAY_NOT_EXISTS") != 0) {
        printf("  error_code is \"%s\"\n", resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    printf("  uniform verdict: %s -> OPRT_ATOP_BUSINESS_ERROR\n", resp.error_code);
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* The header promises "zeroed on entry" on EVERY path, including the argument
 * guards -- a stale error_code surviving a rejected call must not masquerade
 * as this call's cloud verdict. */
static int test_response_zeroed_on_bad_request(void)
{
    iot_atop_response_t resp;
    memset(&resp, 0xAA, sizeof(resp));   /* poison, as stack garbage would */

    iot_atop_request_t req = { .api = "", .version = "1.0" };
    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  returned %d, expected OPRT_INVALID_PARAMETER\n", rt);
        return -1;
    }
    if (resp.result != NULL || resp.error_code[0] != '\0' || resp.server_time != 0) {
        printf("  response was not zeroed on the early-return path\n");
        return -1;
    }
    iot_atop_response_free(&g_client, &resp);   /* must be safe, per the header */
    return 0;
}

/* The whole point of the generic entry: the caller knows what its interface
 * rejects on, so the cloud's own code has to reach it. */
static int test_business_error_carries_error_code(void)
{
    char body[64];
    snprintf(body, sizeof(body), "{\"t\":%u}", (unsigned)time(NULL));  /* no "metas" */

    iot_atop_request_t req = { .api = "tuya.device.meta.save",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_ATOP_BUSINESS_ERROR) {
        printf("  returned %d, expected OPRT_ATOP_BUSINESS_ERROR\n", rt);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (strcmp(resp.error_code, "ILLEGAL_PARAM") != 0) {
        printf("  error_code is \"%s\", expected \"ILLEGAL_PARAM\"\n", resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (resp.result != NULL) {
        printf("  result should be NULL on rejection\n");
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    printf("  rejected with %s (%s)\n", resp.error_code, resp.error_msg);
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

/* An interface the SDK has never heard of still gets a usable answer -- this is
 * what unblocks a business layer from waiting on an SDK release. */
static int test_unknown_api_reaches_the_cloud(void)
{
    char body[64];
    snprintf(body, sizeof(body), "{\"t\":%u}", (unsigned)time(NULL));

    iot_atop_request_t req = { .api = "tuya.device.role.list.get",
                               .version = "1.0",
                               .data = body };
    iot_atop_response_t resp = {0};

    int rt = iot_atop_call(&g_client, &req, &resp);
    if (rt != OPRT_ATOP_BUSINESS_ERROR) {
        printf("  returned %d, expected OPRT_ATOP_BUSINESS_ERROR\n", rt);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    if (strcmp(resp.error_code, "UNKNOWN_API") != 0) {
        printf("  error_code is \"%s\", expected \"UNKNOWN_API\"\n", resp.error_code);
        iot_atop_response_free(&g_client, &resp);
        return -1;
    }
    printf("  signed, encrypted and delivered; cloud replied %s\n", resp.error_code);
    iot_atop_response_free(&g_client, &resp);
    return 0;
}

static int test_response_free_is_repeatable(void)
{
    iot_atop_response_t resp = {0};

    iot_atop_response_free(&g_client, &resp);   /* all-zero struct */

    char body[128];
    snprintf(body, sizeof(body),
             "{\"metas\":{\"k\":\"v\"},\"t\":%u}", (unsigned)time(NULL));
    iot_atop_request_t req = { .api = "tuya.device.meta.save",
                               .version = "1.0",
                               .data = body };
    if (iot_atop_call(&g_client, &req, &resp) != OPRT_OK) {
        printf("  setup call failed\n");
        return -1;
    }

    iot_atop_response_free(&g_client, &resp);
    if (resp.result != NULL || resp.error_code[0] != '\0' || resp.server_time != 0) {
        printf("  free did not zero the struct\n");
        return -1;
    }
    iot_atop_response_free(&g_client, &resp);   /* double free must be safe */
    return 0;
}

int main(void)
{
    printf("========== ATOP Generic Call Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Failed to load CA certificate from %s\n",
                TEST_CONFIG_DIR "/root_cert.pem");
        return 1;
    }

    /* A stack client is enough: iot_atop_call() only reads credentials, the
     * resolved ATOP endpoint, and the TLS settings. */
    memset(&g_client, 0, sizeof(g_client));
    g_client.pal = pal;
    snprintf(g_client.devid, sizeof(g_client.devid), "%s", TEST_DEVID);
    snprintf(g_client.secret_key, sizeof(g_client.secret_key), "%s", TEST_SEC_KEY);
    snprintf(g_client.https_url, sizeof(g_client.https_url),
             "https://%s:%u", MOCK_HOST, MOCK_PORT);
    g_client.cacert = g_cacert;

    if (start_mock_server() != 0) {
        fprintf(stderr, "Failed to start mock server\n");
        pal->free(g_cacert);
        return 1;
    }

    /* Guards — no network needed */
    RUN_TEST(test_null_params);
    RUN_TEST(test_api_and_version_required);
    RUN_TEST(test_requires_device_credentials);
    RUN_TEST(test_body_must_be_json_object);

    /* Round trips */
    RUN_TEST(test_call_returns_result_json);
    RUN_TEST(test_call_returns_json_array);
    RUN_TEST(test_null_body_defaults_to_empty_object);
    RUN_TEST(test_business_error_carries_error_code);
    RUN_TEST(test_unknown_api_reaches_the_cloud);
    RUN_TEST(test_null_result_stays_null);
    RUN_TEST(test_gateway_not_exists_is_business_error);
    RUN_TEST(test_response_zeroed_on_bad_request);
    RUN_TEST(test_response_free_is_repeatable);

    stop_mock_server();
    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
