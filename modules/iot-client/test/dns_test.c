#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "iot_dns.h"
#include "iot_client.h"
#include "iot_config_defaults.h"

#define MOCK_HOST "127.0.0.1"
#define MOCK_PORT 8198

static pid_t mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;

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

static int start_mock(void)
{
    mock_pid = fork();
    if (mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, DNS_MOCK_PATH, NULL);
        perror("execlp dns_mock failed");
        _exit(1);
    }
    if (mock_pid < 0) {
        perror("fork dns_mock");
        return -1;
    }
    printf("DNS mock started (pid %d, port %u)\n", mock_pid, MOCK_PORT);
    sleep(1);
    return OPRT_OK;
}

static void stop_mock(void)
{
    if (mock_pid > 0) {
        printf("Stopping DNS mock (pid %d)...\n", mock_pid);
        kill(mock_pid, SIGTERM);
        waitpid(mock_pid, NULL, 0);
        mock_pid = -1;
    }
}

/* ========== Parameter validation tests ========== */

static int test_dns_query_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int ret = iot_dns_query(pal, NULL, NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", ret);
        return -1;
    }

    iot_dns_query_response_t resp = {0};
    iot_dns_query_request_t req = {0};
    ret = iot_dns_query(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty domains, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_url_config_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int ret = iot_dns_url_config(pal, NULL, NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

static int test_ca_cert_null_params(void)
{
    const pal_t *pal = get_default_pal();
    int ret = iot_dns_get_ca_cert(pal, NULL, NULL);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER, got %d\n", ret);
        return -1;
    }

    iot_dns_ca_cert_response_t resp = {0};
    iot_dns_ca_cert_request_t req = {0};
    ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for NULL target_host, got %d\n", ret);
        return -1;
    }
    return OPRT_OK;
}

/* ========== Connection failure tests ========== */

static int test_dns_query_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_domain_t domains[] = {
        { .domain = "a1.tuyacn.com", .need_ip6 = false },
    };
    iot_dns_query_request_t req = {
        .host = MOCK_HOST,
        .port = 19999,  // Wrong port
        .domains = domains,
        .domain_count = 1,
    };
    iot_dns_query_response_t resp = {0};

    int ret = iot_dns_query(pal, &req, &resp);
    if (ret == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", ret);
    return OPRT_OK;
}

static int test_url_config_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "httpsUrl" },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = 19999,  // Wrong port
        .region = "CN",
        .env = "prod",
        .uuid = "test_uuid_12345678",
        .config = config,
        .config_count = 1,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", ret);
    return OPRT_OK;
}

static int test_ca_cert_connection_fail(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_ca_cert_request_t req = {
        .host = MOCK_HOST,
        .port = 19999,  // Wrong port
        .target_host = "a1.tuyacn.com",
    };
    iot_dns_ca_cert_response_t resp = {0};

    int ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret == OPRT_OK) {
        printf("  expected failure for wrong port, but got success\n");
        iot_dns_ca_cert_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly failed with error %d for unreachable server\n", ret);
    return OPRT_OK;
}

/* ========== Missing parameter tests ========== */

static int test_dns_query_zero_domain_count(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_domain_t domains[] = {
        { .domain = "a1.tuyacn.com", .need_ip6 = false },
    };
    iot_dns_query_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .domains = domains,
        .domain_count = 0,  // Zero domain count
    };
    iot_dns_query_response_t resp = {0};

    int ret = iot_dns_query(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for zero domain_count, got %d\n", ret);
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly rejected zero domain_count\n");
    return OPRT_OK;
}

static int test_ca_cert_empty_target_host(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_ca_cert_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .target_host = "",  // Empty target_host
    };
    iot_dns_ca_cert_response_t resp = {0};

    int ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for empty target_host, got %d\n", ret);
        iot_dns_ca_cert_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly rejected empty target_host\n");
    return OPRT_OK;
}

static int test_url_config_missing_env(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "httpsUrl" },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .region = "CN",
        .uuid = "test_uuid_12345678",
        .config = config,
        .config_count = 1,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for missing env, got %d\n", ret);
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly rejected missing env\n");
    return OPRT_OK;
}

static int test_url_config_missing_uuid(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "httpsUrl" },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .region = "CN",
        .env = "prod",
        .config = config,
        .config_count = 1,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  expected OPRT_INVALID_PARAMETER for missing uuid, got %d\n", ret);
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    printf("  correctly rejected missing uuid\n");
    return OPRT_OK;
}

static int test_url_config_without_region(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "mqttsUrl", .need_ca = true },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .env = "prod",
        .uuid = "test_uuid_12345678",
        .config = config,
        .config_count = 1,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  url_config without region should succeed, got %d\n", ret);
        return -1;
    }
    if (resp.endpoint_count < 1) {
        printf("  expected at least 1 endpoint, got %d\n", resp.endpoint_count);
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    printf("  succeeded without region (on_boarding_with_qrcode scenario)\n");
    iot_dns_url_config_response_free(pal, &resp);
    return OPRT_OK;
}

/* ========== v1/dns_query tests ========== */

static int test_dns_query_single(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_domain_t domains[] = {
        { .domain = "a1.tuyacn.com", .need_ip6 = false },
    };
    iot_dns_query_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .domains = domains,
        .domain_count = 1,
    };
    iot_dns_query_response_t resp = {0};

    int ret = iot_dns_query(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_query failed: %d\n", ret);
        return -1;
    }
    if (resp.result_count != 1) {
        printf("  expected 1 result, got %d\n", resp.result_count);
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }

    iot_dns_domain_result_t *r = &resp.results[0];
    printf("  domain: %s\n", r->domain);
    printf("  ips   : ");
    for (int i = 0; i < r->ip_count; i++)
        printf("%s ", r->ips[i]);
    printf("\n");
    printf("  ttl   : %d\n", r->ttl);

    if (r->ip_count < 1) {
        printf("  expected at least 1 IP\n");
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }
    if (r->ttl <= 0) {
        printf("  expected positive ttl\n");
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }

    iot_dns_query_response_free(pal, &resp);
    return OPRT_OK;
}

static int test_dns_query_with_ipv6(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_domain_t domains[] = {
        { .domain = "a1.tuyacn.com", .need_ip6 = true },
    };
    iot_dns_query_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .domains = domains,
        .domain_count = 1,
    };
    iot_dns_query_response_t resp = {0};

    int ret = iot_dns_query(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_query failed: %d\n", ret);
        return -1;
    }
    if (resp.result_count != 1) {
        printf("  expected 1 result, got %d\n", resp.result_count);
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }

    iot_dns_domain_result_t *r = &resp.results[0];
    printf("  ip6s  : ");
    for (int i = 0; i < r->ip6_count; i++)
        printf("%s ", r->ip6s[i]);
    printf("\n");

    if (r->ip6_count < 1) {
        printf("  expected at least 1 IPv6 address\n");
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }

    iot_dns_query_response_free(pal, &resp);
    return OPRT_OK;
}

static int test_dns_query_multiple(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_domain_t domains[] = {
        { .domain = "a1.tuyacn.com", .need_ip6 = false },
        { .domain = "m1.tuyacn.com", .need_ip6 = false },
    };
    iot_dns_query_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .domains = domains,
        .domain_count = 2,
    };
    iot_dns_query_response_t resp = {0};

    int ret = iot_dns_query(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_query failed: %d\n", ret);
        return -1;
    }
    if (resp.result_count != 2) {
        printf("  expected 2 results, got %d\n", resp.result_count);
        iot_dns_query_response_free(pal, &resp);
        return -1;
    }

    for (int i = 0; i < resp.result_count; i++) {
        printf("  [%d] %s -> %s (ttl=%d)\n", i,
               resp.results[i].domain,
               resp.results[i].ip_count > 0 ? resp.results[i].ips[0] : "(none)",
               resp.results[i].ttl);
    }

    iot_dns_query_response_free(pal, &resp);
    return OPRT_OK;
}

/* ========== v2/url_config tests ========== */

static int test_url_config_basic(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "httpsUrl", .need_ca = true },
        { .key = "httpsPSKUrl", .need_ca = true },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .region = "CN",
        .env = "prod",
        .uuid = "test_uuid_12345678",
        .config = config,
        .config_count = 2,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_url_config failed: %d\n", ret);
        return -1;
    }

    printf("  ttl     : %d\n", resp.ttl);
    printf("  ca_count: %d\n", resp.ca_count);

    if (resp.ttl <= 0) {
        printf("  expected positive ttl\n");
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    if (resp.ca_count < 1) {
        printf("  expected at least 1 CA cert\n");
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }
    if (resp.endpoint_count < 2) {
        printf("  expected 2 endpoints, got %d\n", resp.endpoint_count);
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }

    for (int i = 0; i < resp.endpoint_count; i++) {
        iot_dns_endpoint_t *e = &resp.endpoints[i];
        printf("  [%s] addr=%s  ips=", e->key, e->addr);
        for (int j = 0; j < e->ip_count; j++)
            printf("%s ", e->ips[j]);
        printf("\n");
    }

    iot_dns_url_config_response_free(pal, &resp);
    return OPRT_OK;
}

static int test_url_config_with_region(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = "httpUrl", .need_ip6 = true },
    };
    iot_dns_url_config_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .region = "CN",
        .env = "prod",
        .uuid = "test_uuid_12345678",
        .config = config,
        .config_count = 1,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_url_config failed: %d\n", ret);
        return -1;
    }

    if (resp.endpoint_count < 1) {
        printf("  expected at least 1 endpoint, got %d\n", resp.endpoint_count);
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }

    iot_dns_endpoint_t *e = &resp.endpoints[0];
    printf("  key  : %s\n", e->key);
    printf("  addr : %s\n", e->addr);
    printf("  ip6s : ");
    for (int j = 0; j < e->ip6_count; j++)
        printf("%s ", e->ip6s[j]);
    printf("\n");

    if (e->ip6_count < 1) {
        printf("  expected IPv6 addresses\n");
        iot_dns_url_config_response_free(pal, &resp);
        return -1;
    }

    iot_dns_url_config_response_free(pal, &resp);
    return OPRT_OK;
}

/* ========== GET /api/v1/ca-certificate tests ========== */

static int test_ca_cert_rsa(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_ca_cert_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .target_host = "a1.tuyacn.com",
    };
    iot_dns_ca_cert_response_t resp = {0};

    int ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_get_ca_cert failed: %d\n", ret);
        return -1;
    }

    if (!resp.ca_certificate || resp.ca_certificate[0] == '\0') {
        printf("  expected non-empty ca_certificate\n");
        iot_dns_ca_cert_response_free(pal, &resp);
        return -1;
    }

    printf("  ca_certificate: %.60s...\n", resp.ca_certificate);
    iot_dns_ca_cert_response_free(pal, &resp);
    return OPRT_OK;
}

static int test_ca_cert_ecdsa(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_ca_cert_request_t req = {
        .host = MOCK_HOST,
        .port = MOCK_PORT,
        .target_host = "a1.tuyacn.com",
        .target_port = 8883,
        .public_key_algorithm = "ECDSA",
    };
    iot_dns_ca_cert_response_t resp = {0};

    int ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_get_ca_cert failed: %d\n", ret);
        return -1;
    }

    if (!resp.ca_certificate || resp.ca_certificate[0] == '\0') {
        printf("  expected non-empty ca_certificate\n");
        iot_dns_ca_cert_response_free(pal, &resp);
        return -1;
    }

    printf("  ca_certificate (ECDSA): %.60s...\n", resp.ca_certificate);
    iot_dns_ca_cert_response_free(pal, &resp);
    return OPRT_OK;
}

/* ---------- main ---------- */

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("========== IoT DNS Test Suite ==========\n");

    iot_init(get_default_pal());

    if (start_mock() != 0) {
        fprintf(stderr, "Failed to start DNS mock server\n");
        return 1;
    }
    sleep(1);

    /* Parameter validation */
    RUN_TEST(test_dns_query_null_params);
    RUN_TEST(test_url_config_null_params);
    RUN_TEST(test_ca_cert_null_params);

    /* Connection failure tests */
    RUN_TEST(test_dns_query_connection_fail);
    RUN_TEST(test_url_config_connection_fail);
    RUN_TEST(test_ca_cert_connection_fail);

    /* Missing parameter tests */
    RUN_TEST(test_dns_query_zero_domain_count);
    RUN_TEST(test_ca_cert_empty_target_host);
    RUN_TEST(test_url_config_missing_env);
    RUN_TEST(test_url_config_missing_uuid);

    /* url_config without region (on_boarding_with_qrcode scenario) */
    RUN_TEST(test_url_config_without_region);

    /* v1/dns_query */
    RUN_TEST(test_dns_query_single);
    RUN_TEST(test_dns_query_with_ipv6);
    RUN_TEST(test_dns_query_multiple);

    /* v2/url_config */
    RUN_TEST(test_url_config_basic);
    RUN_TEST(test_url_config_with_region);

    /* GET /api/v1/ca-certificate */
    RUN_TEST(test_ca_cert_rsa);
    RUN_TEST(test_ca_cert_ecdsa);

    stop_mock();

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
