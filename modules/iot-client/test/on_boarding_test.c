#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

#include "iot_on_boarding.h"
#include "iot_client.h"
#include "iot_internal.h"


#define MOCK_DNS_HOST  "127.0.0.1"
#define MOCK_DNS_PORT  8198
#define MOCK_MQTT_PORT 11884
#define MOCK_ATOP_PORT 8443

#define TEST_UUID    "uuid_ci_test_12345678"
#define TEST_AUTHKEY "ci_authkey_1234567890abcdef"
#define TEST_SW_VER  "1.0.0"
#define TEST_PK      "ci_test_product_key"
#define TEST_PV      "2.0"
#define TEST_BV      "1.0"

static pid_t dns_mock_pid = -1;
static pid_t mqtt_mock_pid = -1;
static pid_t atop_mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;
static char g_expected_env_file[] = "/tmp/agentic-kit-dns-env-XXXXXX";
static char g_activation_record_file[] = "/tmp/agentic-kit-atop-record-XXXXXX";

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

/* ---------- helpers ---------- */

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

static void remove_test_files(void)
{
    unlink(g_expected_env_file);
    unlink(g_activation_record_file);
}

static int create_test_files(void)
{
    int env_fd = mkstemp(g_expected_env_file);
    if (env_fd < 0) return -1;
    close(env_fd);
    int record_fd = mkstemp(g_activation_record_file);
    if (record_fd < 0) {
        unlink(g_expected_env_file);
        return -1;
    }
    close(record_fd);
    atexit(remove_test_files);
    return 0;
}

static int write_test_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fputs(content, f) >= 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int no_activation_recorded(void)
{
    FILE *f = fopen(g_activation_record_file, "r");
    if (!f) return 0;
    int first = fgetc(f);
    int ok = first == EOF && !ferror(f);
    fclose(f);
    return ok;
}

/* ---------- Mock server lifecycle ---------- */

static int start_dns_mock(void)
{
    dns_mock_pid = fork();
    if (dns_mock_pid == 0) {
        setenv("DNS_MOCK_USE_SSL", "1", 1);
        setenv("DNS_MOCK_EXPECTED_ENV_FILE", g_expected_env_file, 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, DNS_MOCK_PATH, NULL);
        perror("execlp dns mock failed");
        _exit(1);
    }
    if (dns_mock_pid < 0) {
        perror("fork dns mock");
        return -1;
    }
    printf("DNS mock started (pid %d, port %u)\n", dns_mock_pid, MOCK_DNS_PORT);
    return OPRT_OK;
}

static int start_mqtt_mock(void)
{
    mqtt_mock_pid = fork();
    if (mqtt_mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, ONBOARDING_MQTT_MOCK_PATH, NULL);
        perror("execlp mqtt mock failed");
        _exit(1);
    }
    if (mqtt_mock_pid < 0) {
        perror("fork mqtt mock");
        return -1;
    }
    printf("OnBoarding MQTT mock started (pid %d, port %u)\n", mqtt_mock_pid, MOCK_MQTT_PORT);
    return OPRT_OK;
}

static int start_atop_mock(void)
{
    atop_mock_pid = fork();
    if (atop_mock_pid == 0) {
        setenv("ATOP_MOCK_USE_SSL", "1", 1);
        setenv("ATOP_MOCK_PORT", "8443", 1);
        setenv("ATOP_MOCK_RECORD_FILE", g_activation_record_file, 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, ATOP_MOCK_PATH, NULL);
        perror("execlp atop mock failed");
        _exit(1);
    }
    if (atop_mock_pid < 0) {
        perror("fork atop mock");
        return -1;
    }
    printf("ATOP mock started (pid %d, port %u)\n", atop_mock_pid, MOCK_ATOP_PORT);
    return OPRT_OK;
}

static void stop_mock(pid_t *pid, const char *name)
{
    if (*pid > 0) {
        printf("Stopping %s (pid %d)...\n", name, *pid);
        kill(*pid, SIGTERM);
        waitpid(*pid, NULL, 0);
        *pid = -1;
    }
}

/* Wait until 127.0.0.1:port accepts a TCP connection, up to timeout_ms. The
 * mocks are fork+exec'd Python servers; a blind sleep raced their startup on a
 * loaded CI box (the first real test connect could hit a not-yet-draining accept
 * loop and time out ~8s). A non-blocking connect probe is used so a momentarily
 * saturated backlog can't hang the probe itself. Returns 0 once connectable. */
static int wait_for_port(uint16_t port, int timeout_ms)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, MOCK_DNS_HOST, &addr.sin_addr);

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

/* Barrier: don't start the tests until all three mocks are accepting. */
static int wait_for_mocks(void)
{
    if (wait_for_port(MOCK_DNS_PORT, 15000) != 0) {
        fprintf(stderr, "DNS mock (%u) never became connectable\n", MOCK_DNS_PORT);
        return -1;
    }
    if (wait_for_port(MOCK_MQTT_PORT, 15000) != 0) {
        fprintf(stderr, "MQTT mock (%u) never became connectable\n", MOCK_MQTT_PORT);
        return -1;
    }
    if (wait_for_port(MOCK_ATOP_PORT, 15000) != 0) {
        fprintf(stderr, "ATOP mock (%u) never became connectable\n", MOCK_ATOP_PORT);
        return -1;
    }
    return 0;
}

/* ---------- Test: NULL parameter validation ---------- */

static int test_on_boarding_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int ret = on_boarding_with_qrcode(pal, NULL, NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", ret);
        return -1;
    }

    on_boarding_config_t cfg = {0};
    ret = on_boarding_with_qrcode(pal, &cfg, NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for NULL response, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

/* ---------- Test: full on_boarding_with_qrcode flow ---------- */

static int test_on_boarding_qrcode_flow(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);
    cfg.timeout_ms = 10000;
    cfg.dns_host = MOCK_DNS_HOST;
    cfg.dns_port = MOCK_DNS_PORT;
    cfg.cacert = g_cacert;

    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_qrcode(pal, &cfg, &resp);

    if (ret != OPRT_OK) {
        printf("  on_boarding_with_qrcode failed: %d\n", ret);
        return -1;
    }

    if (resp.devid[0] == '\0') {
        printf("  response missing devid\n");
        return -1;
    }
    if (resp.env != PRE) {
        printf("  App selected PRE, response env=%d\n", resp.env);
        return -1;
    }

    printf("  devid      : %s\n", resp.devid);
    printf("  secret_key : %s\n", resp.secret_key);
    printf("  local_key  : %s\n", resp.local_key);
    printf("  region     : %d\n", resp.region);
    return OPRT_OK;
}

/* ---------- Test: on_boarding_with_token NULL/empty parameter validation ---------- */

static int test_on_boarding_with_token_null_config(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, NULL, "AYsome_token0000", &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for NULL config, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_on_boarding_with_token_null_token(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, NULL, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for NULL token, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_on_boarding_with_token_empty_token(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, "", &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty token, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_on_boarding_with_token_null_response(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    int ret = on_boarding_with_token(pal, &cfg, "AYsome_token0000", NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for NULL response, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

/* ---------- Test: full on_boarding_with_token flow ---------- */

static int run_on_boarding_token_case(const char *app_token, const char *expected_key,
                                      const char *expected_schema_id)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);
    cfg.env = TEST;
    cfg.cacert = g_cacert;
    cfg.dns_host = MOCK_DNS_HOST;
    cfg.dns_port = MOCK_DNS_PORT;

    if (write_test_file(g_expected_env_file, expected_key) != 0) return -1;
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, app_token, &resp);

    if (ret != OPRT_OK) {
        printf("  on_boarding_with_token failed: %d\n", ret);
        return -1;
    }
    if (resp.devid[0] == '\0') {
        printf("  response missing devid\n");
        return -1;
    }
    if (strcmp(resp.registration_key, expected_key) != 0) {
        printf("  raw registration key was not retained\n");
        return -1;
    }
    if (strcmp(resp.schema_id, expected_schema_id) != 0) {
        printf("  activation token was truncated: schema_id=%s\n", resp.schema_id);
        return -1;
    }
    printf("  devid      : %s\n", resp.devid);
    printf("  secret_key : %s\n", resp.secret_key);
    printf("  local_key  : %s\n", resp.local_key);
    printf("  region     : %d\n", resp.region);
    return OPRT_OK;
}

static int test_on_boarding_token_pre(void)
{
    return run_on_boarding_token_case("AYH73H8u7Apr_0", "pr_0", "H73H8u7A");
}

static int test_on_boarding_token_exact_activation_token(void)
{
    return run_on_boarding_token_case("AY12345678pr_0", "pr_0", "12345678");
}

static int test_on_boarding_token_requires_dns_before_activation(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);
    cfg.env = TEST;
    cfg.cacert = g_cacert;
    cfg.dns_host = MOCK_DNS_HOST;
    cfg.dns_port = 19998; /* no DNS service */

    if (write_test_file(g_activation_record_file, "") != 0) return -1;
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, "AY12345678pr_0", &resp);
    if (ret == OPRT_OK || resp.devid[0] != '\0' || !no_activation_recorded()) {
        printf("  activation proceeded without App-selected DNS endpoints: %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_on_boarding_token_daily(void)
{
    return run_on_boarding_token_case("AY12345678da_0", "da_0", "12345678");
}

static int test_on_boarding_token_dns_rejects_wrong_env(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);
    cfg.env = TEST;
    cfg.cacert = g_cacert;
    cfg.dns_host = MOCK_DNS_HOST;
    cfg.dns_port = MOCK_DNS_PORT;

    /* The wire token says pr_0, while this mock invocation expects da_0. */
    if (write_test_file(g_expected_env_file, "da_0") != 0 ||
        write_test_file(g_activation_record_file, "") != 0) return -1;
    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, "AY12345678pr_0", &resp);
    if (ret == OPRT_OK || resp.devid[0] != '\0' || !no_activation_recorded()) {
        printf("  DNS accepted an env different from its test-controlled expectation\n");
        return -1;
    }
    return OPRT_OK;
}

static int test_on_boarding_token_arbitrary_production_key(void)
{
    return run_on_boarding_token_case("AY12345678Q7xZ", "Q7xZ", "12345678");
}

static int test_on_boarding_token_private_cloud_key(void)
{
    return run_on_boarding_token_case("AY12345678x_ab", "x_ab", "12345678");
}

static int test_on_boarding_token_https_url_without_path(void)
{
    return run_on_boarding_token_case("AY12345678NOPA", "NOPA", "12345678");
}

static int test_on_boarding_token_rejects_invalid_input(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    cfg.env = TEST;
    cfg.cacert = g_cacert;

    on_boarding_response_t resp = {0};
    const char *invalid[] = {
        "AY12345678pre",       /* short */
        "AY12345678pr_0x",     /* long */
        "AYci_test_token0000", /* legacy variable-width token */
        "ZZ12345678pr_0",     /* unsupported region */
        "AY1234\x01" "678pr_0", /* control byte in activation token */
        "AY12345678p\x01_0",   /* control byte in registration key */
        "AY12345678p\xC3_0",   /* non-ASCII byte in registration key */
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        memset(&resp, 0, sizeof(resp));
        int ret = on_boarding_with_token(pal, &cfg, invalid[i], &resp);
        if (ret != OPRT_INVALID_PARAMETER || resp.devid[0] != '\0') {
            printf("  malformed token case %zu not rejected: %d\n", i, ret);
            return -1;
        }
    }
    return OPRT_OK;
}

static int test_on_boarding_token_rejects_incomplete_dns(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);
    cfg.env = TEST;
    cfg.cacert = g_cacert;
    cfg.dns_host = MOCK_DNS_HOST;
    cfg.dns_port = MOCK_DNS_PORT;

    const char *tokens[] = {
        "AY12345678MSHT", /* no HTTPS Self endpoint */
        "AY12345678MSMQ", /* no MQTT Self endpoint */
        "AY12345678MALF", /* malformed HTTPS endpoint */
        "AY12345678LONG", /* oversized HTTPS endpoint */
        "AY12345678BADP", /* HTTPS path is not /d.json */
        "AY12345678WSPC", /* whitespace after HTTPS path */
        "AY12345678QURY", /* query string cannot be silently discarded */
        "AY12345678FRAG", /* fragment cannot be silently discarded */
        "AY12345678WHAU", /* whitespace in HTTPS authority */
    };
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) {
        if (write_test_file(g_expected_env_file, tokens[i] + 10) != 0 ||
            write_test_file(g_activation_record_file, "") != 0) return -1;
        on_boarding_response_t resp = {0};
        int ret = on_boarding_with_token(pal, &cfg, tokens[i], &resp);
        if (ret == OPRT_OK || resp.devid[0] != '\0' || !no_activation_recorded()) {
            printf("  bad DNS endpoint case %zu activated: %d\n", i, ret);
            return -1;
        }
    }
    return OPRT_OK;
}

/* ---------- Test: timeout when no activation message ---------- */

static int test_on_boarding_timeout(void)
{
    const pal_t *pal = get_default_pal();
    on_boarding_config_t cfg = {0};
    strncpy(cfg.uuid, TEST_UUID, sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey, TEST_AUTHKEY, sizeof(cfg.authkey) - 1);
    strncpy(cfg.sw_ver, TEST_SW_VER, sizeof(cfg.sw_ver) - 1);
    strncpy(cfg.product_key, TEST_PK, sizeof(cfg.product_key) - 1);
    strncpy(cfg.pv, TEST_PV, sizeof(cfg.pv) - 1);
    strncpy(cfg.bv, TEST_BV, sizeof(cfg.bv) - 1);

    // Point DNS to a port with no server → DNS query fails → should return -1
    cfg.timeout_ms = 1000;
    cfg.dns_host = "127.0.0.1";
    cfg.dns_port = 19998;

    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_qrcode(pal, &cfg, &resp);

    if (ret == 0) {
        printf("  expected failure, but got success\n");
        return -1;
    }
    return OPRT_OK;
}

/* ---------- main ---------- */

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("========== OnBoarding Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Warning: CA cert not loaded, TLS tests may skip verification\n");
    }

    if (create_test_files() != 0) {
        fprintf(stderr, "Failed to create isolated mock control files\n");
        pal->free(g_cacert);
        return 1;
    }

    if (start_dns_mock() != 0 || start_mqtt_mock() != 0 || start_atop_mock() != 0) {
        fprintf(stderr, "Failed to start mock servers\n");
        stop_mock(&dns_mock_pid, "DNS mock");
        stop_mock(&mqtt_mock_pid, "MQTT mock");
        stop_mock(&atop_mock_pid, "ATOP mock");
        pal->free(g_cacert);
        return 1;
    }
    /* Wait until every mock is actually accepting before the first test connects
     * (replaces a blind sleep that raced mock startup on a loaded CI box). */
    if (wait_for_mocks() != 0) {
        stop_mock(&dns_mock_pid, "DNS mock");
        stop_mock(&mqtt_mock_pid, "MQTT mock");
        stop_mock(&atop_mock_pid, "ATOP mock");
        pal->free(g_cacert);
        return 1;
    }

    RUN_TEST(test_on_boarding_null_params);
    RUN_TEST(test_on_boarding_with_token_null_config);
    RUN_TEST(test_on_boarding_with_token_null_token);
    RUN_TEST(test_on_boarding_with_token_empty_token);
    RUN_TEST(test_on_boarding_with_token_null_response);
    RUN_TEST(test_on_boarding_timeout);
    RUN_TEST(test_on_boarding_token_pre);
    RUN_TEST(test_on_boarding_token_exact_activation_token);
    RUN_TEST(test_on_boarding_token_requires_dns_before_activation);
    RUN_TEST(test_on_boarding_token_daily);
    RUN_TEST(test_on_boarding_token_dns_rejects_wrong_env);
    RUN_TEST(test_on_boarding_token_arbitrary_production_key);
    RUN_TEST(test_on_boarding_token_private_cloud_key);
    RUN_TEST(test_on_boarding_token_https_url_without_path);
    RUN_TEST(test_on_boarding_token_rejects_invalid_input);
    RUN_TEST(test_on_boarding_token_rejects_incomplete_dns);
    RUN_TEST(test_on_boarding_qrcode_flow);

    stop_mock(&dns_mock_pid, "DNS mock");
    stop_mock(&mqtt_mock_pid, "MQTT mock");
    stop_mock(&atop_mock_pid, "ATOP mock");

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
