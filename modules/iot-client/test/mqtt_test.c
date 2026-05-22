#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>

#include "mqtt.h"
#include "iot_client.h"
#include "iot_config_defaults.h"

#define TEST_CLIENT_ID   "mqtt_test_client"
#define TEST_USERNAME    "test_user"
#define TEST_PASSWORD    "test_pass"
#define TEST_TOPIC_SUB   "test/sub"
#define TEST_TOPIC_PUB   "test/pub"

#define MOCK_BROKER_URL      "mqtt://127.0.0.1:11883"
#define MOCK_BROKER_URL_TLS  "ssl://127.0.0.1:18883"

static pid_t mock_pid = -1;
static pid_t mock_tls_pid = -1;
static char *g_cacert = NULL;
static int tests_run = 0;
static int tests_passed = 0;

static volatile bool g_msg_received = false;
static char g_msg_topic[256];
static uint8_t g_msg_payload[1024];
static size_t g_msg_payload_len = 0;

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

static int start_mock_server(void)
{
    mock_pid = fork();
    if (mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MQTT_MOCK_SCRIPT_PATH, NULL);
        perror("execlp failed");
        _exit(1);
    }
    if (mock_pid < 0) {
        perror("fork");
        return -1;
    }
    printf("MQTT mock server started (pid %d), waiting...\n", mock_pid);
    sleep(1);
    return OPRT_OK;
}

static int start_mock_tls_server(void)
{
    mock_tls_pid = fork();
    if (mock_tls_pid == 0) {
        setenv("MQTT_MOCK_USE_TLS", "1", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MQTT_MOCK_SCRIPT_PATH, NULL);
        perror("execlp failed");
        _exit(1);
    }
    if (mock_tls_pid < 0) {
        perror("fork");
        return -1;
    }
    printf("MQTT TLS mock server started (pid %d), waiting...\n", mock_tls_pid);
    sleep(1);
    return OPRT_OK;
}

static void stop_mock_server(void)
{
    if (mock_pid > 0) {
        printf("Stopping MQTT mock server (pid %d)...\n", mock_pid);
        kill(mock_pid, SIGTERM);
        waitpid(mock_pid, NULL, 0);
        mock_pid = -1;
    }
}

static void stop_mock_tls_server(void)
{
    if (mock_tls_pid > 0) {
        printf("Stopping MQTT TLS mock server (pid %d)...\n", mock_tls_pid);
        kill(mock_tls_pid, SIGTERM);
        waitpid(mock_tls_pid, NULL, 0);
        mock_tls_pid = -1;
    }
}

/* ---------- Message callback ---------- */

static void test_message_callback(const char *topic, size_t topic_len,
                                  const uint8_t *payload, size_t payload_len,
                                  void *user_data)
{
    (void)user_data;
    if (topic_len < sizeof(g_msg_topic)) {
        memcpy(g_msg_topic, topic, topic_len);
        g_msg_topic[topic_len] = '\0';
    }
    size_t copy_len = payload_len < sizeof(g_msg_payload) ? payload_len : sizeof(g_msg_payload);
    memcpy(g_msg_payload, payload, copy_len);
    g_msg_payload_len = copy_len;
    g_msg_received = true;
}

/* ---------- Test: NULL parameter validation ---------- */

static int test_create_null_params(void)
{
    mqtt_client *c = mqtt_client_create_with_config(NULL);
    if (c != NULL) {
        printf("  expected NULL from mqtt_client_create_with_config(NULL)\n");
        mqtt_client_destroy(c);
        return -1;
    }
    return OPRT_OK;
}

/* ---------- Test: create and destroy ---------- */

static int test_create_destroy(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  mqtt_client_create_with_config failed\n");
        return -1;
    }
    if (mqtt_client_is_connected(c)) {
        printf("  expected not connected after create\n");
        mqtt_client_destroy(c);
        return -1;
    }
    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: create with config ---------- */

static int test_create_with_config(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url = MOCK_BROKER_URL,
        .client_id = TEST_CLIENT_ID,
        .username = TEST_USERNAME,
        .password = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback = test_message_callback,
        .tls_config = NULL,
        .pal = pal,
    };

    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  mqtt_client_create_with_config failed\n");
        return -1;
    }
    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: connect / disconnect ---------- */

static int test_connect_disconnect(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    if (mqtt_client_connect(c) != 0) {
        printf("  connect failed\n");
        mqtt_client_destroy(c);
        return -1;
    }
    if (!mqtt_client_is_connected(c)) {
        printf("  expected connected after connect\n");
        mqtt_client_destroy(c);
        return -1;
    }

    mqtt_client_disconnect(c);
    if (mqtt_client_is_connected(c)) {
        printf("  expected not connected after disconnect\n");
        mqtt_client_destroy(c);
        return -1;
    }

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: subscribe ---------- */

static int test_subscribe(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) return -1;

    if (mqtt_client_connect(c) != 0) {
        printf("  connect failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    if (mqtt_client_subscribe(c) != 0) {
        printf("  subscribe failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: publish and receive echo ---------- */

static int test_publish_receive(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) return -1;

    if (mqtt_client_connect(c) != 0) {
        printf("  connect failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    if (mqtt_client_subscribe(c) != 0) {
        printf("  subscribe failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    const char *msg = "hello mqtt test";
    if (mqtt_client_publish(c, TEST_TOPIC_PUB,
                            (const uint8_t *)msg, strlen(msg)) != 0) {
        printf("  publish failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    g_msg_received = false;
    int retries = 20;
    while (!g_msg_received && retries-- > 0) {
        int rc = mqtt_client_process(c, 100);
        if (rc != 0) break;
        usleep(50000);
    }

    if (!g_msg_received) {
        printf("  did not receive echo message\n");
        mqtt_client_destroy(c);
        return -1;
    }

    if (strcmp(g_msg_topic, TEST_TOPIC_SUB) != 0) {
        printf("  unexpected topic: %s (expected %s)\n", g_msg_topic, TEST_TOPIC_SUB);
        mqtt_client_destroy(c);
        return -1;
    }

    if (g_msg_payload_len != strlen(msg) ||
        memcmp(g_msg_payload, msg, strlen(msg)) != 0) {
        printf("  unexpected payload\n");
        mqtt_client_destroy(c);
        return -1;
    }

    printf("  echo topic: %s, payload: %.*s\n",
           g_msg_topic, (int)g_msg_payload_len, g_msg_payload);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: connect failure (bad password) ---------- */

static int test_connect_auth_fail(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = "wrong_password",
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    int ret = mqtt_client_connect(c);
    if (ret == 0) {
        printf("  expected connect to fail with bad password\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  connect correctly failed with ret=%d (bad credentials)\n", ret);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: subscribe failure ---------- */

static int test_subscribe_fail(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = "fail/rejected_topic",
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    if (mqtt_client_connect(c) != 0) {
        printf("  connect failed\n");
        mqtt_client_destroy(c);
        return -1;
    }

    int ret = mqtt_client_subscribe(c);
    if (ret == 0) {
        printf("  expected subscribe to fail for fail/ topic\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  subscribe correctly failed with ret=%d\n", ret);

    mqtt_client_destroy(c);
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

static int test_connect_tls_cert_fail(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_tls_config_t tls_config = {
        .cacert = WRONG_CA_CERT,
    };
    mqtt_client_config_t config = {
        .broker_url = MOCK_BROKER_URL_TLS,
        .client_id = TEST_CLIENT_ID,
        .username = TEST_USERNAME,
        .password = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback = test_message_callback,
        .tls_config = &tls_config,
        .pal = pal,
    };

    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    int ret = mqtt_client_connect(c);
    if (ret == 0) {
        printf("  expected TLS failure with wrong CA cert, but got success\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  connect correctly failed with ret=%d (TLS cert verification)\n", ret);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: TLS connect + MQTT auth failure (leak regression) ---------- */

static int test_connect_tls_auth_fail(void)
{
    const pal_t *pal = get_default_pal();
    if (!g_cacert) {
        printf("  skipped (no CA certificate loaded)\n");
        return OPRT_OK;
    }

    mqtt_tls_config_t tls_config = {
        .cacert = g_cacert,
    };
    mqtt_client_config_t config = {
        .broker_url = MOCK_BROKER_URL_TLS,
        .client_id = TEST_CLIENT_ID,
        .username = TEST_USERNAME,
        .password = "wrong_password",
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback = test_message_callback,
        .tls_config = &tls_config,
        .pal = pal,
    };

    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    int ret = mqtt_client_connect(c);
    if (ret == 0) {
        printf("  expected MQTT auth failure over TLS, but got success\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  connect correctly failed with ret=%d (TLS ok, MQTT auth rejected)\n", ret);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: TLS handshake failure (plain TCP server) ---------- */

static int test_connect_tls_handshake_fail(void)
{
    const pal_t *pal = get_default_pal();
    if (!g_cacert) {
        printf("  skipped (no CA certificate loaded)\n");
        return OPRT_OK;
    }

    mqtt_tls_config_t tls_config = {
        .cacert = g_cacert,
    };
    mqtt_client_config_t config = {
        .broker_url = "ssl://127.0.0.1:11883",
        .client_id = TEST_CLIENT_ID,
        .username = TEST_USERNAME,
        .password = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback = test_message_callback,
        .tls_config = &tls_config,
        .pal = pal,
    };

    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    int ret = mqtt_client_connect(c);
    if (ret == 0) {
        printf("  expected TLS handshake failure against plain TCP server\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  connect correctly failed with ret=%d (TLS handshake failed)\n", ret);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: TLS TCP connection failure (unreachable host) ---------- */

static int test_connect_tls_unreachable(void)
{
    const pal_t *pal = get_default_pal();
    if (!g_cacert) {
        printf("  skipped (no CA certificate loaded)\n");
        return OPRT_OK;
    }

    mqtt_tls_config_t tls_config = {
        .cacert = g_cacert,
    };
    mqtt_client_config_t config = {
        .broker_url = "ssl://127.0.0.1:19999",
        .client_id = TEST_CLIENT_ID,
        .username = TEST_USERNAME,
        .password = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback = test_message_callback,
        .tls_config = &tls_config,
        .pal = pal,
    };

    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    int ret = mqtt_client_connect(c);
    if (ret == 0) {
        printf("  expected connection failure to unreachable TLS host\n");
        mqtt_client_destroy(c);
        return -1;
    }
    printf("  connect correctly failed with ret=%d (TLS unreachable)\n", ret);

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: connect failure (bad URL) ---------- */

static int test_connect_bad_url(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = "tcp://127.0.0.1:19999",
        .client_id       = TEST_CLIENT_ID,
        .password        = TEST_PASSWORD,
        .subscribe_topic = TEST_TOPIC_SUB,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    if (mqtt_client_connect(c) == 0) {
        printf("  expected connect to fail on bad port\n");
        mqtt_client_destroy(c);
        return -1;
    }

    mqtt_client_destroy(c);
    return OPRT_OK;
}

/* ---------- Test: operations on NULL client ---------- */

static int test_null_client_ops(void)
{
    if (mqtt_client_connect(NULL) == 0) {
        printf("  connect(NULL) should return error\n");
        return -1;
    }
    if (mqtt_client_subscribe(NULL) == 0) {
        printf("  subscribe(NULL) should return error\n");
        return -1;
    }
    if (mqtt_client_publish(NULL, "t", (const uint8_t *)"m", 1) == 0) {
        printf("  publish(NULL) should return error\n");
        return -1;
    }
    if (mqtt_client_process(NULL, 100) == 0) {
        printf("  process(NULL) should return error\n");
        return -1;
    }
    if (mqtt_client_is_connected(NULL) != false) {
        printf("  is_connected(NULL) should return false\n");
        return -1;
    }
    mqtt_client_disconnect(NULL);
    mqtt_client_destroy(NULL);
    return OPRT_OK;
}

/* ---------- main ---------- */

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("========== MQTT Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Warning: Failed to load CA certificate from %s\n",
                TEST_CONFIG_DIR "/root_cert.pem");
    }

    if (start_mock_server() != 0) {
        fprintf(stderr, "Failed to start MQTT mock server\n");
        return 1;
    }

    if (start_mock_tls_server() != 0) {
        fprintf(stderr, "Failed to start MQTT TLS mock server\n");
        stop_mock_server();
        return 1;
    }

    RUN_TEST(test_create_null_params);
    RUN_TEST(test_null_client_ops);
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_create_with_config);
    RUN_TEST(test_connect_disconnect);
    RUN_TEST(test_subscribe);
    RUN_TEST(test_publish_receive);
    RUN_TEST(test_connect_auth_fail);
    RUN_TEST(test_subscribe_fail);
    RUN_TEST(test_connect_tls_cert_fail);
    RUN_TEST(test_connect_tls_auth_fail);
    RUN_TEST(test_connect_tls_handshake_fail);
    RUN_TEST(test_connect_tls_unreachable);
    RUN_TEST(test_connect_bad_url);

    stop_mock_tls_server();
    stop_mock_server();

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
