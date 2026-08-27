/**
 * @file iot_session_token_test.c
 * @brief Tests for iot_client_get_session_token() and its _ex variant.
 *
 * The interesting case is a rejection: the cloud refuses to issue an agent
 * token and says why in errorCode. All of "device unbound", "privacy agreement
 * unsigned" and "no agent configured" arrive on this one path as the same
 * return code, so the caller can only tell them apart -- and a device can only
 * react correctly, retry vs. re-provision vs. stop -- if errorCode survives.
 *
 * The client is a stack iot_client_t with devid/secret_key/https_url set, so no
 * activation or MQTT connection is needed; https_url points the ATOP host
 * resolution at the mock server. The mock rejects any agentCode of the form
 * "reject:<CODE>" with that errorCode.
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

#include "iot_client.h"
#include "iot_config_defaults.h"
#include "log.h"

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

/* ---------- Mock server lifecycle (same probe as iot_atop_call_test) ---------- */

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

/* ---------- Guards ---------- */

static int test_null_params(void)
{
    char token[128] = {0};

    if (iot_client_get_session_token(NULL, "agent", token, sizeof(token)) != OPRT_INVALID_PARAMETER ||
        iot_client_get_session_token(&g_client, "agent", NULL, sizeof(token)) != OPRT_INVALID_PARAMETER ||
        iot_client_get_session_token(&g_client, "agent", token, 0) != OPRT_INVALID_PARAMETER) {
        printf("  NULL guards failed\n");
        return -1;
    }
    return 0;
}

/* The _ex variant guards the same way, and a rejection buffer must not be a
 * way to sneak past them. */
static int test_ex_null_params(void)
{
    char token[128] = {0};
    iot_atop_rejection_t rejection;
    memset(&rejection, 'x', sizeof(rejection));

    int rt = iot_client_get_session_token_ex(NULL, "agent", token, sizeof(token), &rejection);
    if (rt != OPRT_INVALID_PARAMETER) {
        printf("  returned %d, expected OPRT_INVALID_PARAMETER\n", rt);
        return -1;
    }
    if (rejection.code[0] != '\0' || rejection.msg[0] != '\0') {
        printf("  rejection not cleared on a rejected argument\n");
        return -1;
    }
    return 0;
}

/* ---------- Round trips ---------- */

static int test_token_round_trip(void)
{
    /* "token" is the whole session blob -- the connect and session config the
     * caller then parses -- so it needs room, not the 32 bytes a bare token
     * would take. */
    char token[2048] = {0};

    int rt = iot_client_get_session_token(&g_client, "agent_alpha", token, sizeof(token));
    if (rt != OPRT_OK) {
        printf("  returned %d, expected OPRT_OK\n", rt);
        return -1;
    }
    if (strstr(token, "\"agentToken\":\"mock_token_agent_alpha\"") == NULL) {
        printf("  token blob carries no agentToken: %s\n", token);
        return -1;
    }
    return 0;
}

/* On success there is nothing to explain, so the rejection stays empty. */
static int test_success_leaves_rejection_empty(void)
{
    char token[2048] = {0};
    iot_atop_rejection_t rejection;
    memset(&rejection, 'x', sizeof(rejection));

    int rt = iot_client_get_session_token_ex(&g_client, "agent_alpha", token,
                                             sizeof(token), &rejection);
    if (rt != OPRT_OK) {
        printf("  returned %d, expected OPRT_OK\n", rt);
        return -1;
    }
    if (rejection.code[0] != '\0' || rejection.msg[0] != '\0') {
        printf("  rejection is \"%s\"/\"%s\", expected empty\n", rejection.code, rejection.msg);
        return -1;
    }
    return 0;
}

/* The whole point: the cloud's verdict reaches the caller. */
static int test_rejection_carries_error_code(void)
{
    const char *codes[] = {
        "GATEWAY_NOT_EXISTS",               /* device removed from the cloud */
        "CHILD_PRIVACY_AGREEMENT_REQUIRED", /* agreement not signed yet */
        "ISSUE_TOKEN_FAILED",               /* no agent configured for the product */
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        char agent_code[96];
        char token[128] = {0};
        iot_atop_rejection_t rejection = {0};

        snprintf(agent_code, sizeof(agent_code), "reject:%s", codes[i]);
        int rt = iot_client_get_session_token_ex(&g_client, agent_code, token,
                                                 sizeof(token), &rejection);
        if (rt == OPRT_OK) {
            printf("  \"%s\" unexpectedly succeeded\n", codes[i]);
            return -1;
        }
        if (strcmp(rejection.code, codes[i]) != 0) {
            printf("  code is \"%s\", expected \"%s\"\n", rejection.code, codes[i]);
            return -1;
        }
        if (rejection.msg[0] == '\0') {
            printf("  \"%s\" carried no errorMsg\n", codes[i]);
            return -1;
        }
        printf("  %s -> rt=%d code=\"%s\" msg=\"%s\"\n", codes[i], rt,
               rejection.code, rejection.msg);
    }
    return 0;
}

/* A caller that does not care must still be able to pass NULL. */
static int test_rejection_buffer_is_optional(void)
{
    char token[128] = {0};

    int rt = iot_client_get_session_token_ex(&g_client, "reject:ISSUE_TOKEN_FAILED",
                                             token, sizeof(token), NULL);
    if (rt == OPRT_OK) {
        printf("  unexpectedly succeeded\n");
        return -1;
    }
    /* Same path through the old signature, which is now a wrapper. */
    rt = iot_client_get_session_token(&g_client, "reject:ISSUE_TOKEN_FAILED",
                                      token, sizeof(token));
    if (rt == OPRT_OK) {
        printf("  wrapper unexpectedly succeeded\n");
        return -1;
    }
    return 0;
}

/* A token that does not fit is a caller bug, not a cloud rejection: the
 * response is well formed, so nothing should be reported as refused. */
static int test_short_buffer_is_not_a_rejection(void)
{
    char token[4] = {0};   /* far too small for the session blob */
    iot_atop_rejection_t rejection;
    memset(&rejection, 'x', sizeof(rejection));

    int rt = iot_client_get_session_token_ex(&g_client, "agent_alpha", token,
                                             sizeof(token), &rejection);
    if (rt != OPRT_INVALID_RESULT) {
        printf("  returned %d, expected OPRT_INVALID_RESULT\n", rt);
        return -1;
    }
    if (rejection.code[0] != '\0') {
        printf("  reported rejection \"%s\" for a local buffer problem\n", rejection.code);
        return -1;
    }
    return 0;
}

int main(void)
{
    printf("========== Session Token Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Failed to load CA certificate from %s\n",
                TEST_CONFIG_DIR "/root_cert.pem");
        return 1;
    }

    /* A stack client is enough: the token call only reads credentials, the
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
    RUN_TEST(test_ex_null_params);

    /* Round trips */
    RUN_TEST(test_token_round_trip);
    RUN_TEST(test_success_leaves_rejection_empty);
    RUN_TEST(test_rejection_carries_error_code);
    RUN_TEST(test_rejection_buffer_is_optional);
    RUN_TEST(test_short_buffer_is_not_a_rejection);

    stop_mock_server();
    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
