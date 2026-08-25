/**
 * @file iot_reset_test.c
 * @brief Tests for device-initiated reset (iot_client_reset).
 *
 * The contract under test is the asymmetry: success destroys the client,
 * failure leaves it fully usable. Both halves matter -- a reset that tore the
 * client down on a "server is busy" rejection would leave the device locally
 * unbound while the cloud still considers it bound, with no handle left to
 * retry through.
 *
 * The client here is heap-allocated on purpose. iot_client_reset() ends in
 * iot_client_deinit(), which frees the struct, so the stack client used by
 * iot_atop_call_test.c would hand free() a stack address. Fields are filled by
 * hand rather than via iot_client_init() to keep the test off the DNS /
 * meta-save round trips that init performs.
 *
 * Whether the success path actually releases everything is not observable from
 * inside the process -- run test/run_leaks_check.sh or run_valgrind_check.sh
 * for that. This suite deliberately neither touches nor frees the client after
 * a successful reset, so a leak or a double free shows up under those tools.
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

#define MOCK_HOST "127.0.0.1"
#define MOCK_PORT 8443

/* Must match test/config/atop.conf (device_id / sec_key). */
#define TEST_DEVID   "ci_device_test_001"
#define TEST_SEC_KEY "1234567890abcdef"

/* The mock decrypts with sec_key whatever devId is presented, and answers a
 * devId containing "busy" with the interface doc's own retryable rejection. */
#define BUSY_DEVID   "busy_device_test_001"

static pid_t mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;
static const pal_t *g_pal = NULL;

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

/* ---------- Mock server lifecycle (same probe as iot_atop_call_test) ------- */

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
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, ATOP_MOCK_PATH, NULL);
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

/* A heap client shaped like the one iot_client_init() returns, minus the
 * network round trips. Zeroed elsewhere, so iot_client_deinit() finds mqtt,
 * dp and schema all NULL and skips them. */
static iot_client_t *make_client(const char *devid)
{
    iot_client_t *client = (iot_client_t *)g_pal->malloc(sizeof(iot_client_t));
    if (!client) return NULL;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = g_pal;
    snprintf(client->devid, sizeof(client->devid), "%s", devid);
    snprintf(client->secret_key, sizeof(client->secret_key), "%s", TEST_SEC_KEY);
    snprintf(client->https_url, sizeof(client->https_url),
             "https://%s:%u", MOCK_HOST, MOCK_PORT);
    client->cacert = g_cacert;
    return client;
}

/* ---------- Guards: rejected locally, no round trip ---------- */

static int test_null_client(void)
{
    if (iot_client_reset(NULL, NULL, 0) != OPRT_INVALID_PARAMETER) {
        printf("  iot_client_reset(NULL, NULL, 0) must return OPRT_INVALID_PARAMETER\n");
        return -1;
    }
    return 0;
}

/* Before activation there is nothing to unbind -- and no credential to sign
 * with. Must fail without destroying the client. */
static int test_requires_device_credentials(void)
{
    int result = 0;

    iot_client_t *no_devid = make_client("");
    if (!no_devid) return -1;
    if (iot_client_reset(no_devid, NULL, 0) != OPRT_UNINITIALIZED) {
        printf("  empty devid must return OPRT_UNINITIALIZED\n");
        result = -1;
    }
    /* Still intact: readable, and ours to free. */
    if (no_devid->pal != g_pal) {
        printf("  client was disturbed on the guard path\n");
        result = -1;
    }
    iot_client_deinit(no_devid);

    iot_client_t *no_key = make_client(TEST_DEVID);
    if (!no_key) return -1;
    no_key->secret_key[0] = '\0';
    if (iot_client_reset(no_key, NULL, 0) != OPRT_UNINITIALIZED) {
        printf("  empty secret_key must return OPRT_UNINITIALIZED\n");
        result = -1;
    }
    iot_client_deinit(no_key);

    return result;
}

/* ---------- Round trips ---------- */

/* The success envelope carries an EMPTY result object ({}), per the interface
 * doc. Reaching OPRT_OK here is what proves the wrapper does not require a
 * populated result -- an insistence on one would turn every success into an
 * error.
 *
 * This also pins the interface version: the mock rejects anything but v=3.0,
 * so a slip in ATOP_DEVICE_RESET_VERSION fails here rather than showing up as
 * an opaque cloud rejection on a real device.
 *
 * The client is deliberately not touched or freed afterwards: it is gone, and
 * the leak checkers are what verify that. */
static int test_reset_success_destroys_client(void)
{
    iot_client_t *client = make_client(TEST_DEVID);
    if (!client) return -1;

    char error_code[64] = "unset";
    int rc = iot_client_reset(client, error_code, sizeof(error_code));
    if (error_code[0] != '\0') {
        printf("  errorCode should be empty on success, got \"%s\"\n", error_code);
    }
    if (rc != OPRT_OK) {
        printf("  returned %d, expected OPRT_OK\n", rc);
        iot_client_deinit(client);   /* failure path: still ours to free */
        return -1;
    }
    printf("  cloud accepted an empty-result envelope; client released\n");
    return 0;
}

/* The core of the contract. A rejected reset must leave everything standing:
 * the caller has to be able to retry, and a device that tore itself down here
 * would be locally unbound while the cloud still has it bound. */
static int test_reset_failure_keeps_client(void)
{
    iot_client_t *client = make_client(BUSY_DEVID);
    if (!client) return -1;

    int result = 0;
    char error_code[64] = {0};
    int rc = iot_client_reset(client, error_code, sizeof(error_code));
    if (rc != OPRT_ATOP_BUSINESS_ERROR) {
        printf("  returned %d, expected OPRT_ATOP_BUSINESS_ERROR\n", rc);
        result = -1;
    }
    /* The whole reason the out-param exists: -14 alone cannot tell a terminal
     * rejection from a retryable one. The mock answers a busy server here. */
    if (strcmp(error_code, "REMOTE_API_RUN_UNKNOW_FAILED") != 0) {
        printf("  errorCode is \"%s\", expected REMOTE_API_RUN_UNKNOW_FAILED\n",
               error_code);
        result = -1;
    }

    /* Usable after the rejection: fields readable, credentials unchanged. */
    if (strcmp(client->devid, BUSY_DEVID) != 0 ||
        strcmp(client->secret_key, TEST_SEC_KEY) != 0) {
        printf("  credentials were disturbed by a failed reset\n");
        result = -1;
    }
    /* And retryable -- the same call again reaches the cloud again. */
    if (iot_client_reset(client, NULL, 0) != OPRT_ATOP_BUSINESS_ERROR) {
        printf("  client was not retryable after a failed reset\n");
        result = -1;
    }

    iot_client_deinit(client);   /* failure means teardown is still the caller's */
    return result;
}

int main(void)
{
    printf("========== Device Reset Test Suite ==========\n");

    g_pal = get_default_pal();
    iot_init(g_pal);

    g_cacert = load_file(g_pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Failed to load CA certificate from %s\n",
                TEST_CONFIG_DIR "/root_cert.pem");
        return 1;
    }

    if (start_mock_server() != 0) {
        fprintf(stderr, "Failed to start mock server\n");
        g_pal->free(g_cacert);
        return 1;
    }

    /* Guards — no network needed */
    RUN_TEST(test_null_client);
    RUN_TEST(test_requires_device_credentials);

    /* Round trips */
    RUN_TEST(test_reset_success_destroys_client);
    RUN_TEST(test_reset_failure_keeps_client);

    stop_mock_server();
    g_pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
