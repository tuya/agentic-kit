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

#include "iot_dns.h"
#include "iot_client.h"
#include "iot_client_message.h"
#include "iot_internal.h"
#include "iot_dp_internal.h"

#define MOCK_HOST "127.0.0.1"
#define MOCK_PORT 8198

static pid_t mock_pid = -1;
static pid_t tls_mock_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char expected_env_file[] = "/tmp/agentic-dns-env-XXXXXX";
static pal_t fail_dns_pal;
static int failed_dns_connects = 0;
static char *test_cacert = NULL;
static int wait_for_port(uint16_t port, int timeout_ms);

static char *load_test_cacert(void)
{
    FILE *file = fopen(TEST_CONFIG_DIR "/root_cert.pem", "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    char *pem = malloc((size_t)size + 1);
    if (!pem) { fclose(file); return NULL; }
    size_t read_size = fread(pem, 1, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) { free(pem); return NULL; }
    pem[size] = '\0';
    return pem;
}

static void *always_fail_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
    (void)host;
    (void)port;
    (void)timeout_ms;
    failed_dns_connects++;
    return NULL;
}

static int expect_dns_env(const char *env)
{
    int fd = open(expected_env_file, O_WRONLY | O_TRUNC);
    if (fd < 0) return -1;
    size_t len = strlen(env);
    int ok = write(fd, env, len) == (ssize_t)len;
    close(fd);
    return ok ? 0 : -1;
}

static void init_keyed_client(iot_client_t *client, const char *key)
{
    memset(client, 0, sizeof(*client));
    client->pal = get_default_pal();
    client->region = AY;
    snprintf(client->devid, sizeof(client->devid), "ci_device_test_001");
    client->cacert = test_cacert;
    memcpy(client->registration_key, key, 5);
}

static int test_keyed_dns_requires_a_trusted_bootstrap_ca(void)
{
    iot_client_t client;
    init_keyed_client(&client, "pr_0");
    client.cacert = NULL;
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret != OPRT_INVALID_PARAMETER) {
        printf("  keyed DNS accepted missing bootstrap CA/bundle: %d\n", ret);
        return -1;
    }
    return 0;
}

static int test_keyed_dns_routes_raw_env_and_self_endpoints(void)
{
    iot_client_t client;
    init_keyed_client(&client, "pr_0");
    if (expect_dns_env("pr_0") != 0) return -1;
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret != OPRT_OK || strcmp(client.https_url, "https://127.0.0.1:443/d.json") != 0 ||
        strcmp(client.mqtt_url, "mqtts://127.0.0.1:11884") != 0) {
        printf("  keyed DNS did not return Self endpoints: %d\n", ret);
        return -1;
    }
    char host[64] = {0};
    uint16_t port = 0;
    if (iot_client_resolve_atop_host(&client, host, sizeof(host), &port) != OPRT_OK ||
        strcmp(host, "127.0.0.1") != 0 || port != 443) return -1;
    return 0;
}

static int test_keyed_dns_missing_endpoint_clears_both(void)
{
    iot_client_t client;
    init_keyed_client(&client, "MSMQ");
    if (expect_dns_env("MSMQ") != 0) return -1;
    snprintf(client.https_url, sizeof(client.https_url), "https://old.example.com/d.json");
    snprintf(client.mqtt_url, sizeof(client.mqtt_url), "mqtts://old.example.com:8883");
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret == OPRT_OK || client.https_url[0] || client.mqtt_url[0]) {
        printf("  missing Self endpoint reused old route: %d\n", ret);
        return -1;
    }
    return 0;
}

static int test_keyed_dns_oversize_clears_both(void)
{
    iot_client_t client;
    init_keyed_client(&client, "LONG");
    if (expect_dns_env("LONG") != 0) return -1;
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret == OPRT_OK || client.https_url[0] || client.mqtt_url[0]) return -1;
    return 0;
}

static int test_keyed_dns_missing_https_clears_both(void)
{
    iot_client_t client;
    init_keyed_client(&client, "MSHT");
    if (expect_dns_env("MSHT") != 0) return -1;
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret == OPRT_OK || client.https_url[0] || client.mqtt_url[0]) return -1;
    return 0;
}

static int test_keyed_dns_overflowed_port_is_rejected(void)
{
    iot_client_t client;
    init_keyed_client(&client, "OVFL");
    if (expect_dns_env("OVFL") != 0) return -1;
    int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
    if (ret == OPRT_OK || client.https_url[0] || client.mqtt_url[0]) {
        printf("  overflowing port was accepted: %d\n", ret);
        return -1;
    }
    return 0;
}

static int test_registration_key_is_bounded_printable_string(void)
{
    iot_client_config_t cfg = {0};
    cfg.skip_version_report = true;
    const char bad[][5] = {
        {'a', 'b', 'c', '\0', 'x'},
        {'a', 'b', 'c', 'd', 'e'},
        {'a', '\0', 'c', 'd', '\0'},
        {'a', 'b', '\n', 'd', '\0'},
        {'\0', 'b', 'c', 'd', '\0'},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        memcpy(cfg.registration_key, bad[i], 5);
        iot_client_t *client = iot_client_init(&cfg);
        if (client) {
            iot_client_deinit(client);
            printf("  accepted malformed key shape %zu\n", i);
            return -1;
        }
    }
    memset(cfg.registration_key, 0, sizeof(cfg.registration_key));
    iot_client_t *legacy = iot_client_init(&cfg);
    if (!legacy) return -1;
    iot_client_deinit(legacy);
    return 0;
}

static int test_keyed_dns_accepts_qr_registration_keys(void)
{
    const char keys[][5] = {"pro", "pre", "prod", "a", "ab", "abc"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        iot_client_t client;
        init_keyed_client(&client, keys[i]);
        if (expect_dns_env(keys[i]) != 0) return -1;
        int ret = iot_client_dns_resolve(&client, MOCK_HOST, 8199);
        int ok = ret == OPRT_OK && client.https_url[0] && client.mqtt_url[0];
        client.pal->free(client.owned_self_cacert);
        if (!ok) {
            printf("  Raw QR key %s was rejected/rewritten: %d\n", keys[i], ret);
            return -1;
        }
    }
    return 0;
}

static int test_keyed_client_rejects_plaintext_mqtt_configuration(void)
{
    iot_client_config_t cfg = {0};
    cfg.skip_version_report = true;
    cfg.mqtt_disable_tls = true;
    memcpy(cfg.registration_key, "pr_0", 5);
    iot_client_t *client = iot_client_init(&cfg);
    if (client) {
        iot_client_deinit(client);
        printf("  keyed client accepted unsupported plaintext MQTT route\n");
        return -1;
    }
    return 0;
}

static int test_keyed_init_retains_credentials_and_retries_dns(void)
{
    fail_dns_pal = *get_default_pal();
    fail_dns_pal.tcp_connect = always_fail_tcp_connect;
    failed_dns_connects = 0;
    if (iot_init(&fail_dns_pal) != OPRT_OK) return -1;

    iot_client_config_t cfg = {0};
    snprintf(cfg.devid, sizeof(cfg.devid), "device-with-credentials");
    snprintf(cfg.secret_key, sizeof(cfg.secret_key), "secret-for-test");
    snprintf(cfg.local_key, sizeof(cfg.local_key), "local-key-for-test");
    cfg.cacert = test_cacert;
    memcpy(cfg.registration_key, "pr_0", 5);
    iot_client_t *client = iot_client_init(&cfg);
    int first_attempts = failed_dns_connects;
    int reconnect_ret = client ? iot_client_connect(client) : OPRT_OK;
    int all_attempts = failed_dns_connects;
    int ok = client && !client->mqtt && client->https_url[0] == '\0' &&
             client->mqtt_url[0] == '\0' &&
             strcmp(client->registration_key, "pr_0") == 0 &&
             strcmp(client->devid, cfg.devid) == 0 &&
             strcmp(client->secret_key, cfg.secret_key) == 0 &&
             first_attempts > 0 && all_attempts > first_attempts &&
             reconnect_ret != OPRT_OK;
    if (client) iot_client_deinit(client);
    iot_init(get_default_pal());
    if (!ok) {
        printf("  keyed init/reconnect lost credentials or skipped DNS retry\n");
        return -1;
    }
    return 0;
}

static int test_keyed_init_retains_credentials_after_mqtt_failure(void)
{
    fail_dns_pal = *get_default_pal();
    fail_dns_pal.tcp_connect = always_fail_tcp_connect;
    if (iot_init(&fail_dns_pal) != OPRT_OK) return -1;
    iot_client_config_t cfg = {0};
    snprintf(cfg.secret_key, sizeof(cfg.secret_key), "1234567890abcdef");
    snprintf(cfg.local_key, sizeof(cfg.local_key), "0123456789abcdef");
    cfg.region = AY;
    cfg.skip_version_report = true;
    cfg.cacert = test_cacert;
    cfg.mqtt_disable_auto_connect = true;

    iot_client_t *client = iot_client_init(&cfg);
    if (client) {
        snprintf(client->devid, sizeof(client->devid), "ci_device_test_001");
        memcpy(client->registration_key, "pr_0", 5);
        snprintf(client->https_url, sizeof(client->https_url), "https://127.0.0.1:443/d.json");
        snprintf(client->mqtt_url, sizeof(client->mqtt_url), "mqtts://127.0.0.1:19998");
        client->self_cacert = test_cacert;
    }
    int connect_ret = client ? iot_client_message_connect(client) : OPRT_INVALID_PARAMETER;
    int credentials_ok = client &&
             strcmp(client->devid, "ci_device_test_001") == 0 &&
             strcmp(client->secret_key, cfg.secret_key) == 0 &&
             strcmp(client->local_key, cfg.local_key) == 0;
    int key_ok = client && strcmp(client->registration_key, "pr_0") == 0;
    int routes_ok = client &&
             strcmp(client->mqtt_url, "mqtts://127.0.0.1:19998") == 0 &&
             strcmp(client->https_url, "https://127.0.0.1:443/d.json") == 0;
    int ok = client && client->mqtt == NULL && connect_ret != OPRT_OK &&
             credentials_ok && key_ok && routes_ok;
    if (!ok) {
        printf("  keyed MQTT failure state: client=%d mqtt=%d ret=%d credentials=%d key=%d routes=%d\n",
               client != NULL, client && client->mqtt != NULL, connect_ret,
               credentials_ok, key_ok, routes_ok);
    }
    if (client) iot_client_deinit(client);
    iot_init(get_default_pal());
    if (!ok) {
        return -1;
    }
    return 0;
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
    if (wait_for_port(MOCK_PORT, 15000) != 0) {
        fprintf(stderr, "DNS mock (%u) never became connectable\n", MOCK_PORT);
        return -1;
    }
    return OPRT_OK;
}

static int start_tls_mock(void)
{
    tls_mock_pid = fork();
    if (tls_mock_pid == 0) {
        setenv("DNS_MOCK_USE_SSL", "1", 1);
        setenv("DNS_MOCK_PORT", "8199", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, DNS_MOCK_PATH, NULL);
        _exit(1);
    }
    if (tls_mock_pid < 0) return -1;
    return wait_for_port(8199, 15000);
}

static void stop_tls_mock(void)
{
    if (tls_mock_pid > 0) {
        kill(tls_mock_pid, SIGTERM);
        waitpid(tls_mock_pid, NULL, 0);
        tls_mock_pid = -1;
    }
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

/* ========== Static region -> host mapping ========== */

/* Every enum region must map to its own PROD ATOP host — a region falling
 * through to the China default silently sends ATOP traffic to the wrong data
 * center (regression: SG was missing and hit a1.tuyacn.com). */
static int test_region_to_host_prod_mapping(void)
{
    static const struct { iot_region_t region; const char *host; } cases[] = {
        { AY,   IOT_CN_HOST },
        { AZ,   IOT_AZ_HOST },
        { UEAZ, IOT_UEAZ_HOST },
        { EU,   IOT_EU_HOST },
        { WEAZ, IOT_WEAZ_HOST },
        { IN,   IOT_IN_HOST },
        { SG,   IOT_SG_HOST },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *host = iot_region_to_host(cases[i].region, PROD);
        if (!host || strcmp(host, cases[i].host) != 0) {
            printf("  region %d: expected %s, got %s\n",
                   (int)cases[i].region, cases[i].host, host ? host : "(null)");
            return -1;
        }
    }
    printf("  all %zu regions map to their own PROD host\n",
           sizeof(cases) / sizeof(cases[0]));
    return OPRT_OK;
}

/* The `region` field of POST /v2/url_config must carry the code the service
 * knows, which is the two-letter activation-token prefix — NOT the enum's own
 * name. Sending the enum spelling ("UEAZ"/"WEAZ") is answered with HTTP 200 and
 * a body carrying ttl/caArr but no endpoint objects, so the query "succeeds"
 * while resolving nothing and mqtt_url stays empty (regression: US-East and
 * West-Europe devices never reached the MQTT connect at all).
 *
 * Keep this table identical to __token_to_region() in iot_on_boarding.c: the two
 * are inverses, and it was them drifting apart that produced the bug. */
static int test_region_to_string_wire_codes(void)
{
    static const struct { iot_region_t region; const char *code; } cases[] = {
        { AY,   "AY" },
        { AZ,   "AZ" },
        { UEAZ, "UE" },
        { EU,   "EU" },
        { WEAZ, "WE" },
        { IN,   "IN" },
        { SG,   "SG" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *code = iot_region_to_string(cases[i].region);
        if (!code || strcmp(code, cases[i].code) != 0) {
            printf("  region %d: expected \"%s\", got \"%s\"\n",
                   (int)cases[i].region, cases[i].code, code ? code : "(null)");
            return -1;
        }
    }
    if (iot_region_to_string((iot_region_t)999) != NULL) {
        printf("  an unknown region must map to NULL, not a wrong code\n");
        return -1;
    }
    printf("  all %zu regions map to their wire code\n",
           sizeof(cases) / sizeof(cases[0]));
    return OPRT_OK;
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

/* The keys the SDK itself asks for must resolve. Requesting a key the service
 * does not publish is not an error: the response comes back 200 with the
 * endpoint object simply absent, so a typo'd or stale key resolves nothing and
 * every caller downstream sees an empty URL with no failure to trace.
 *
 * This pins IOT_DNS_KEY_* against the mock's catalog. It cannot prove the real
 * service publishes them — only a live query does that — but it does fail the
 * moment a constant is edited without the fixture being taught the new name,
 * which is the prompt to go re-check the service. */
static int test_url_config_sdk_keys_resolve(void)
{
    const pal_t *pal = get_default_pal();
    iot_dns_config_item_t config[] = {
        { .key = IOT_DNS_KEY_MQTTS },
        { .key = IOT_DNS_KEY_HTTPS },
        { .key = IOT_DNS_KEY_MQTT  },
    };
    const int n_keys = (int)(sizeof(config) / sizeof(config[0]));
    iot_dns_url_config_request_t req = {
        .host         = MOCK_HOST,
        .port         = MOCK_PORT,
        .region       = "AY",
        .env          = "prod",
        .uuid         = "test_uuid_12345678",
        .config       = config,
        .config_count = n_keys,
    };
    iot_dns_url_config_response_t resp = {0};

    int ret = iot_dns_url_config(pal, &req, &resp);
    if (ret != OPRT_OK) {
        printf("  iot_dns_url_config failed: %d\n", ret);
        return -1;
    }

    int rc = OPRT_OK;
    for (int k = 0; k < n_keys; k++) {
        const char *addr = NULL;
        for (int i = 0; i < resp.endpoint_count; i++) {
            if (strcmp(resp.endpoints[i].key, config[k].key) == 0) {
                addr = resp.endpoints[i].addr;
                break;
            }
        }
        if (!addr || addr[0] == '\0') {
            printf("  %s: NOT RESOLVED — the SDK asks for this key, so an empty\n"
                   "      result here is a silently unreachable service\n",
                   config[k].key);
            rc = -1;
        } else {
            printf("  %-12s -> %s\n", config[k].key, addr);
        }
    }

    iot_dns_url_config_response_free(pal, &resp);
    return rc;
}

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
    test_cacert = load_test_cacert();
    if (!test_cacert) return 1;

    int env_fd = mkstemp(expected_env_file);
    if (env_fd < 0) return 1;
    close(env_fd);
    setenv("DNS_MOCK_EXPECTED_ENV_FILE", expected_env_file, 1);
    setenv("ATOP_MOCK_PORT", "443", 1);

    if (start_mock() != 0) {
        fprintf(stderr, "Failed to start DNS mock server\n");
        return 1;
    }
    sleep(1);

    /* Static region -> host mapping */
    RUN_TEST(test_region_to_host_prod_mapping);
    RUN_TEST(test_region_to_string_wire_codes);

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
    RUN_TEST(test_url_config_sdk_keys_resolve);
    RUN_TEST(test_url_config_basic);
    RUN_TEST(test_url_config_with_region);
    if (start_tls_mock() != 0) return 1;
    RUN_TEST(test_keyed_dns_requires_a_trusted_bootstrap_ca);
    RUN_TEST(test_keyed_dns_routes_raw_env_and_self_endpoints);
    RUN_TEST(test_keyed_dns_missing_endpoint_clears_both);
    RUN_TEST(test_keyed_dns_oversize_clears_both);
    RUN_TEST(test_keyed_dns_missing_https_clears_both);
    RUN_TEST(test_keyed_dns_overflowed_port_is_rejected);
    RUN_TEST(test_keyed_dns_accepts_qr_registration_keys);
    stop_tls_mock();
    RUN_TEST(test_registration_key_is_bounded_printable_string);
    RUN_TEST(test_keyed_client_rejects_plaintext_mqtt_configuration);
    RUN_TEST(test_keyed_init_retains_credentials_and_retries_dns);
    RUN_TEST(test_keyed_init_retains_credentials_after_mqtt_failure);

    /* GET /api/v1/ca-certificate */
    RUN_TEST(test_ca_cert_rsa);
    RUN_TEST(test_ca_cert_ecdsa);

    stop_mock();
    free(test_cacert);
    unlink(expected_env_file);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
