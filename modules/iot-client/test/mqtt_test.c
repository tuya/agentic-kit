#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdarg.h>   /* capture_log_handler takes a va_list */

#include "mqtt.h"
#include "iot_client.h"
#include "iot_config_defaults.h"
#include "log.h"
#include "tls.h"   /* TLS key log */

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

/* ---------- Test: the broker's refusal reason reaches the log ---------- */

/* coreMQTT knows which of the five MQTT 3.1.1 refusal reasons the broker sent,
 * but only says so through its Log* macros. Those expand to nothing unless the
 * build picks up common/core_mqtt_config.h, and nothing else in this suite
 * notices if it stops doing so: test_connect_auth_fail passes either way, since
 * it only asserts that connect failed.
 *
 * So assert the routing itself. Without it the caller is left with a bare
 * MQTTServerRefused and no way to tell "wrong password" from "device unbound in
 * the cloud" -- which is the whole reason the wiring exists. */

static char log_capture[4096];
static size_t log_capture_len;
static log_fn_t saved_log_fn;

static void capture_log_handler(log_level_t level, const char *fmt, va_list args)
{
    /* va_copy is mandatory, not tidiness: vsnprintf() below consumes `args`, and
     * handing a consumed va_list to log_default_handler() -- which vfprintf()s
     * it again -- is undefined behaviour (C11 7.16.1). It happened to work on
     * macOS/arm64 and produced parameter-shifted garbage on Linux/x86-64, where
     * a bogus "host:port" sent a test off connecting to nowhere until the CI
     * timeout killed it. */
    va_list copy;
    va_copy(copy, args);
    char line[512];
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
        /* vsnprintf returns the length it WOULD have written, so it exceeds
         * the buffer on a truncated message -- copying `n` reads past `line`.
         * Routed lines do get long: one coreHTTP parse error interpolates up to
         * a whole response buffer. */
    size_t len = (n > 0 && (size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1;
    if (n > 0 && log_capture_len + len + 1 < sizeof(log_capture)) {
        memcpy(log_capture + log_capture_len, line, len);
        log_capture_len += len;
        log_capture[log_capture_len++] = '\n';
        log_capture[log_capture_len] = '\0';
    }
    /* Keep the default output too, so a failing run is still readable. */
    log_default_handler(level, fmt, args);
}

static void capture_start(void)
{
    log_capture[0] = '\0';
    log_capture_len = 0;
    saved_log_fn = capture_log_handler;
    log_set_handler(capture_log_handler);
}

static void capture_stop(void)
{
    log_set_handler(NULL);
    (void)saved_log_fn;
}

static int test_connack_reason_is_logged(void)
{
    const pal_t *pal = get_default_pal();
    mqtt_client_config_t config = {
        .broker_url      = MOCK_BROKER_URL,
        .client_id       = TEST_CLIENT_ID,
        .password        = "wrong_password",   /* mock answers CONNACK code 4 */
        .subscribe_topic = TEST_TOPIC_SUB,
        .callback        = test_message_callback,
        .pal             = pal,
    };
    mqtt_client *c = mqtt_client_create_with_config(&config);
    if (!c) {
        printf("  create failed\n");
        return -1;
    }

    capture_start();
    int ret = mqtt_client_connect(c);
    capture_stop();

    int result = 0;
    if (ret == 0) {
        printf("  expected connect to fail with bad password\n");
        result = -1;
    }

    /* coreMQTT's own words, routed through common/core_mqtt_config.h. */
    if (strstr(log_capture, "bad user name or password") == NULL) {
        printf("  the broker's refusal reason never reached the log --\n");
        printf("  is MQTT_DO_NOT_USE_CUSTOM_CONFIG back, or common/ off coreMQTT's include path?\n");
        result = -1;
    }
    /* And the SDK's own line names the status symbolically. This must match the
     * message too, not just the enum name: coreMQTT itself logs "MQTT connection
     * failed with status = MQTTServerRefused", captured by the same handler, so a
     * bare search for the enum name passes even with mqtt.c back on a raw "%d". */
    if (strstr(log_capture, "MQTT_Connect failed: MQTTServerRefused") == NULL) {
        printf("  the SDK's own log line lost the symbolic status name\n");
        result = -1;
    }

    mqtt_client_destroy(c);
    return result;
}

/* ---------- Test: a closed peer is reported, not polled forever ---------- */

/* transport_recv() must translate EOF into an error. The PAL contract (pal.h)
 * defines a 0 from tcp_recv as EOF, but coreMQTT reads a 0 from the transport as
 * "nothing to read right now" -- so handing it straight through means a dropped
 * link goes unnoticed until the 60 s keepalive expires. The TLS branch always
 * got this right and says so in a comment; the TCP branch (mqtt_disable_tls=true)
 * did not, until this test was written.
 *
 * Killing the broker mid-session is what a dropped link looks like from here.
 * The teardown that follows also walks mqtt_client_disconnect()'s dead-link
 * path, which is worth exercising even though its DISCONNECT suppression cannot
 * be asserted here: a just-killed peer usually still absorbs one send, so the
 * failure coreMQTT would log does not reliably occur against a mock. */
static int test_closed_peer_is_reported(void)
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

    stop_mock_server();          /* the link dies under us */

    int result = 0;
    /* Drive the loop until it notices. One call is usually enough; a few gives
     * the socket time to report the peer going away. */
    int rc = OPRT_OK;
    for (int i = 0; i < 20 && rc == OPRT_OK; i++) {
        rc = mqtt_client_process(c, 100);
    }
    if (rc == OPRT_OK) {
        printf("  the process loop never noticed the peer had closed --\n");
        printf("  does transport_recv() still hand coreMQTT a bare 0 on EOF?\n");
        mqtt_client_destroy(c);
        start_mock_server();
        return -1;
    }

    /* The teardown an app's reconnect loop performs. Asserted only to not crash
     * or hang; see the note above on why the DISCONNECT suppression itself is
     * not observable against a mock. */
    mqtt_client_disconnect(c);
    if (mqtt_client_is_connected(c)) {
        printf("  still reports connected after disconnect\n");
        result = -1;
    }

    mqtt_client_destroy(c);
    start_mock_server();         /* restore for the tests that follow */
    return result;
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

/* ---------- Tests: TLS key log ---------- */

/* One TLS handshake against the TLS mock, straight through tls_connect() with
 * the same profile mqtt.c uses (TLS 1.2, Tuya suites): every key-log line is
 * produced inside the handshake, before any MQTT byte, so the MQTT layer adds
 * nothing here. The mock tolerates a peer that closes right after the
 * handshake. Returns 0 if the handshake completed. */
static int keylog_do_one_tls_handshake(void)
{
    tls_config_t cfg = {
        .host         = "127.0.0.1",
        .port         = 18883,
        .sni          = "127.0.0.1",
        .cacert       = g_cacert,
        .force_tls12  = true,
        .ciphersuites = tls_ciphersuites_tuya_default(),
        .pal          = get_default_pal(),
    };
    tls_t *t = tls_connect(&cfg);
    if (!t) return -1;
    tls_close(t);
    return 0;
}

static int  g_keylog_lines = 0;
static char g_keylog_last[512];

static void keylog_capture(void *ctx, const char *line)
{
    (void)ctx;
    g_keylog_lines++;
    snprintf(g_keylog_last, sizeof(g_keylog_last), "%s", line);
}

/* `label` ' ' 64 lowercase hex (client random) ' ' secret_hex_len lowercase hex
 * '\n' -- and nothing after it: Wireshark wants exactly one line. */
static bool keylog_line_is_well_formed(const char *line, const char *label,
                                       size_t secret_hex_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t label_len = strlen(label);
    if (strncmp(line, label, label_len) != 0 || line[label_len] != ' ') return false;
    const char *p = line + label_len + 1;
    if (strspn(p, hex) != 64 || p[64] != ' ') return false;
    p += 65;
    if (strspn(p, hex) != secret_hex_len) return false;
    return p[secret_hex_len] == '\n' && p[secret_hex_len + 1] == '\0';
}

/* iot-client pins TLS 1.2, so one handshake yields exactly one CLIENT_RANDOM
 * line carrying the 48-byte master secret; clearing the sink must stop them.
 * Also pins the "key logging ENABLED" warning the docs tell operators to grep
 * production logs for. */
static int test_keylog_handler_captures_client_random(void)
{
    g_keylog_lines = 0;
    g_keylog_last[0] = '\0';

    capture_start();
    tls_set_keylog_handler(keylog_capture, NULL);
    capture_stop();
    if (strstr(log_capture, "key logging ENABLED") == NULL) {
        printf("  enabling the sink did not log the documented warning\n");
        tls_set_keylog_handler(NULL, NULL);
        return -1;
    }

    int ret = keylog_do_one_tls_handshake();
    tls_set_keylog_handler(NULL, NULL);

    if (ret != 0) {
        printf("  TLS connect failed\n");
        return -1;
    }
    if (g_keylog_lines != 1) {
        printf("  expected 1 key-log line, got %d\n", g_keylog_lines);
        return -1;
    }
    if (!keylog_line_is_well_formed(g_keylog_last, "CLIENT_RANDOM", 96)) {
        printf("  malformed key-log line: %s", g_keylog_last);
        return -1;
    }

    /* Disabling must actually disable: no further lines after the handler is
     * cleared, or a debug session would keep leaking secrets. */
    g_keylog_lines = 0;
    if (keylog_do_one_tls_handshake() != 0) {
        printf("  second TLS connect failed\n");
        return -1;
    }
    if (g_keylog_lines != 0) {
        printf("  handler still called after being cleared (%d lines)\n", g_keylog_lines);
        return -1;
    }
    return OPRT_OK;
}

static int test_keylog_file_sink(void)
{
    const pal_t *pal = get_default_pal();
    char path[256];
    snprintf(path, sizeof(path), "/tmp/ak_keylog_test_%d.log", (int)getpid());
    unlink(path);
    int rc = -1;
    char *content = NULL;

    if (tls_keylog_open_file(path) != TLS_OK) {
        printf("  tls_keylog_open_file failed\n");
        goto out;
    }
    /* A second open while one is active must be refused rather than silently
     * leaking the first FILE*. */
    if (tls_keylog_open_file(path) == TLS_OK) {
        printf("  expected second tls_keylog_open_file to fail\n");
        goto out;
    }
    /* Replacing the file sink with a custom one must not leave the file open:
     * the file must be complete (and closable twice) after the swap. */
    if (keylog_do_one_tls_handshake() != 0) {
        printf("  TLS connect failed\n");
        goto out;
    }
    tls_keylog_close_file();
    tls_keylog_close_file();   /* idempotent */

    /* The whole file, not the first line: the validator wants "\n\0" right after
     * the secret, so this also proves exactly one line was written. */
    content = load_file(pal, path);
    if (!content || content[0] == '\0') {
        printf("  key log file missing or empty\n");
        goto out;
    }
    if (!keylog_line_is_well_formed(content, "CLIENT_RANDOM", 96)) {
        printf("  key log file does not hold exactly one well-formed line: %s", content);
        goto out;
    }
#if defined(__unix__) || defined(__APPLE__)
    struct stat st;
    if (stat(path, &st) != 0 || (st.st_mode & 0777) != 0600) {
        printf("  key log file mode is %o, expected 0600\n",
               (unsigned)(st.st_mode & 0777));
        goto out;
    }
#endif
    rc = OPRT_OK;
out:
    tls_keylog_close_file();
    unlink(path);           /* never leave session keys behind, pass or fail */
    if (content) pal->free(content);
    return rc;
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
    RUN_TEST(test_connack_reason_is_logged);
    RUN_TEST(test_closed_peer_is_reported);
    RUN_TEST(test_subscribe_fail);
    RUN_TEST(test_connect_tls_cert_fail);
    RUN_TEST(test_connect_tls_auth_fail);
    RUN_TEST(test_connect_tls_handshake_fail);
    RUN_TEST(test_connect_tls_unreachable);
    if (g_cacert) {
        RUN_TEST(test_keylog_handler_captures_client_random);
        RUN_TEST(test_keylog_file_sink);
    } else {
        printf("\n  key-log tests skipped (no CA certificate loaded)\n");
    }
    RUN_TEST(test_connect_bad_url);

    stop_mock_tls_server();
    stop_mock_server();

    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
