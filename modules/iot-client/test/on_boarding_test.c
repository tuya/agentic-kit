#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "iot_on_boarding.h"
#include "iot_client.h"
#include "iot_config_defaults.h"


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

/* ---------- Mock server lifecycle ---------- */

static int start_dns_mock(void)
{
    dns_mock_pid = fork();
    if (dns_mock_pid == 0) {
        setenv("DNS_MOCK_USE_SSL", "1", 1);
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

static int test_on_boarding_with_token_flow(void)
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

    on_boarding_response_t resp = {0};
    int ret = on_boarding_with_token(pal, &cfg, "AYci_test_token0000", &resp);

    if (ret != OPRT_OK) {
        printf("  on_boarding_with_token failed: %d\n", ret);
        return -1;
    }
    if (resp.devid[0] == '\0') {
        printf("  response missing devid\n");
        return -1;
    }
    printf("  devid      : %s\n", resp.devid);
    printf("  secret_key : %s\n", resp.secret_key);
    printf("  local_key  : %s\n", resp.local_key);
    printf("  region     : %d\n", resp.region);
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

    if (start_dns_mock() != 0 || start_mqtt_mock() != 0 || start_atop_mock() != 0) {
        fprintf(stderr, "Failed to start mock servers\n");
        stop_mock(&dns_mock_pid, "DNS mock");
        stop_mock(&mqtt_mock_pid, "MQTT mock");
        stop_mock(&atop_mock_pid, "ATOP mock");
        pal->free(g_cacert);
        return 1;
    }
    sleep(2);

    RUN_TEST(test_on_boarding_null_params);
    RUN_TEST(test_on_boarding_with_token_null_config);
    RUN_TEST(test_on_boarding_with_token_null_token);
    RUN_TEST(test_on_boarding_with_token_empty_token);
    RUN_TEST(test_on_boarding_with_token_null_response);
    RUN_TEST(test_on_boarding_timeout);
    RUN_TEST(test_on_boarding_with_token_flow);
    RUN_TEST(test_on_boarding_qrcode_flow);

    stop_mock(&dns_mock_pid, "DNS mock");
    stop_mock(&mqtt_mock_pid, "MQTT mock");
    stop_mock(&atop_mock_pid, "ATOP mock");

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
