#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "iot_client.h"
#include "iot_client_message.h"
#include "iot_config_defaults.h"


#define TEST_DEVID      "test_device_msg_001"
#define TEST_SECRET_KEY "abcdef1234567890"
#define TEST_LOCAL_KEY  "0123456789abcdef"
#define TEST_MQTT_URL   "mqtts://127.0.0.1:11885"

#define RAW_TEST_MESSAGE "{\"type\":\"test\",\"payload\":\"hello_from_mock\"}"

static pid_t mock_pid = -1;
static pid_t mock_invalid_pid = -1;
static pid_t mock_wrongkey_pid = -1;
static int tests_run = 0;
static int tests_passed = 0;
static char *g_cacert = NULL;

#define MOCK_PORT_NORMAL     11885
#define MOCK_PORT_INVALID    11886
#define MOCK_PORT_WRONGKEY   11887

static volatile int cb_called = 0;
static char cb_topic[256] = {0};
static uint8_t cb_data[4096] = {0};
static size_t cb_data_len = 0;

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

static void reset_callback_state(void)
{
    cb_called = 0;
    memset(cb_topic, 0, sizeof(cb_topic));
    memset(cb_data, 0, sizeof(cb_data));
    cb_data_len = 0;
}

static void test_message_callback(const char *topic, size_t topic_len,
                                  const uint8_t *data, size_t data_len)
{
    cb_called++;
    size_t tlen = topic_len < sizeof(cb_topic) - 1 ? topic_len : sizeof(cb_topic) - 1;
    memcpy(cb_topic, topic, tlen);
    cb_topic[tlen] = '\0';

    size_t dlen = data_len < sizeof(cb_data) ? data_len : sizeof(cb_data);
    memcpy(cb_data, data, dlen);
    cb_data_len = dlen;

    printf("  [CALLBACK] topic=%s, data_len=%zu, data=%.*s\n",
           cb_topic, cb_data_len, (int)cb_data_len, cb_data);
}

static int wait_for_port(int port)
{
    for (int i = 0; i < 50; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);
        if (ret == 0) return OPRT_OK;
        usleep(20000);
    }
    return -1;
}

static int wait_for_callback(iot_client_t *client, int max_attempts)
{
    for (int i = 0; i < max_attempts && cb_called == 0; i++) {
        int ret = iot_client_message_process(client, 50);
        if (ret != OPRT_OK) return ret;
    }
    return cb_called > 0 ? OPRT_OK : -1;
}

/* ---------- mock lifecycle ---------- */

static int start_mock(void)
{
    mock_pid = fork();
    if (mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        perror("execlp message mock failed");
        _exit(1);
    }
    if (mock_pid < 0) {
        perror("fork message mock");
        return -1;
    }
    printf("Message mock started (pid %d)\n", mock_pid);
    return wait_for_port(MOCK_PORT_NORMAL);
}

static int start_mock_invalid_format(void)
{
    mock_invalid_pid = fork();
    if (mock_invalid_pid == 0) {
        setenv("MESSAGE_MOCK_TYPE", "invalid_format", 1);
        setenv("MESSAGE_MOCK_PORT", "11886", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        perror("execlp message mock (invalid) failed");
        _exit(1);
    }
    if (mock_invalid_pid < 0) {
        perror("fork message mock (invalid)");
        return -1;
    }
    printf("Message mock (invalid format) started (pid %d)\n", mock_invalid_pid);
    return wait_for_port(MOCK_PORT_INVALID);
}

static int start_mock_wrong_key(void)
{
    mock_wrongkey_pid = fork();
    if (mock_wrongkey_pid == 0) {
        setenv("MESSAGE_MOCK_TYPE", "wrong_key_encrypted", 1);
        setenv("MESSAGE_MOCK_PORT", "11887", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        perror("execlp message mock (wrongkey) failed");
        _exit(1);
    }
    if (mock_wrongkey_pid < 0) {
        perror("fork message mock (wrongkey)");
        return -1;
    }
    printf("Message mock (wrong key) started (pid %d)\n", mock_wrongkey_pid);
    return wait_for_port(MOCK_PORT_WRONGKEY);
}

static void stop_mock(void)
{
    if (mock_pid > 0) {
        printf("Stopping message mock (pid %d)...\n", mock_pid);
        kill(mock_pid, SIGTERM);
        waitpid(mock_pid, NULL, 0);
        mock_pid = -1;
    }
}

static void stop_mock_invalid(void)
{
    if (mock_invalid_pid > 0) {
        printf("Stopping message mock invalid (pid %d)...\n", mock_invalid_pid);
        kill(mock_invalid_pid, SIGTERM);
        waitpid(mock_invalid_pid, NULL, 0);
        mock_invalid_pid = -1;
    }
}

static void stop_mock_wrongkey(void)
{
    if (mock_wrongkey_pid > 0) {
        printf("Stopping message mock wrongkey (pid %d)...\n", mock_wrongkey_pid);
        kill(mock_wrongkey_pid, SIGTERM);
        waitpid(mock_wrongkey_pid, NULL, 0);
        mock_wrongkey_pid = -1;
    }
}

/* ---------- Test 1: raw message (pv23_decrypt fails, raw forwarded) ---------- */

static int test_raw_message(void)
{
    reset_callback_state();

    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;

    strncpy((char *)client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy((char *)client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy((char *)client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    client->mqtt_url = TEST_MQTT_URL;
    client->cacert = g_cacert;
    client->message_callback = test_message_callback;

    int ret = iot_client_message_connect(client);
    if (ret != OPRT_OK) {
        printf("  iot_client_message_connect failed: %d\n", ret);
        pal->free(client);
        return -1;
    }

    wait_for_callback(client, 30);

    iot_client_message_disconnect(client);

    int result = -1;
    if (cb_called < 1) {
        printf("  callback not called\n");
    } else {
        char expected_topic[256];
        snprintf(expected_topic, sizeof(expected_topic), "smart/device/in/%s", TEST_DEVID);
        if (strcmp(cb_topic, expected_topic) != 0) {
            printf("  topic mismatch: expected '%s', got '%s'\n", expected_topic, cb_topic);
        } else if (cb_data_len != strlen(RAW_TEST_MESSAGE) ||
                   memcmp(cb_data, RAW_TEST_MESSAGE, cb_data_len) != 0) {
            printf("  data mismatch: expected '%s', got '%.*s'\n",
                   RAW_TEST_MESSAGE, (int)cb_data_len, cb_data);
        } else {
            printf("  topic: %s\n", cb_topic);
            printf("  data : %.*s\n", (int)cb_data_len, cb_data);
            result = 0;
        }
    }

    /* don't free mqtt_url/cacert — they point to static/global data */
    client->mqtt_url = NULL;
    client->cacert = NULL;
    pal->free(client);
    return result;
}

/* ---------- Test 2: invalid format message (decryption fails, raw forwarded) ---------- */

static int test_invalid_format_message(void)
{
    reset_callback_state();

    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;

    strncpy((char *)client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy((char *)client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy((char *)client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    client->mqtt_url = "mqtts://127.0.0.1:11886";
    client->cacert = g_cacert;
    client->message_callback = test_message_callback;

    int ret = iot_client_message_connect(client);
    if (ret != OPRT_OK) {
        printf("  iot_client_message_connect failed: %d\n", ret);
        pal->free(client);
        return -1;
    }

    wait_for_callback(client, 30);

    iot_client_message_disconnect(client);

    int result = -1;
    if (cb_called < 1) {
        printf("  callback not called\n");
    } else {
        printf("  callback called with raw data (len=%zu)\n", cb_data_len);
        if (cb_data_len > 0) {
            printf("  invalid format message correctly forwarded as raw\n");
            result = 0;
        }
    }

    client->mqtt_url = NULL;
    client->cacert = NULL;
    pal->free(client);
    return result;
}

/* ---------- Test 3: wrong key decryption failure ---------- */

static int test_decrypt_fail_wrong_key(void)
{
    reset_callback_state();

    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;

    strncpy((char *)client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy((char *)client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy((char *)client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    client->mqtt_url = "mqtts://127.0.0.1:11887";
    client->cacert = g_cacert;
    client->message_callback = test_message_callback;

    int ret = iot_client_message_connect(client);
    if (ret != OPRT_OK) {
        printf("  iot_client_message_connect failed: %d\n", ret);
        pal->free(client);
        return -1;
    }

    wait_for_callback(client, 30);

    iot_client_message_disconnect(client);

    int result = -1;
    if (cb_called < 1) {
        printf("  callback not called\n");
    } else {
        printf("  callback called with raw data (len=%zu)\n", cb_data_len);
        if (cb_data_len > 0) {
            printf("  wrong-key encrypted message correctly forwarded as raw\n");
            result = 0;
        }
    }

    client->mqtt_url = NULL;
    client->cacert = NULL;
    pal->free(client);
    return result;
}

/* ---------- Test 4: encrypted message (pv23 encrypt→echo→decrypt) ---------- */

static int test_encrypted_message(void)
{
    reset_callback_state();

    const pal_t *pal = get_default_pal();
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (!client) return -1;
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;

    strncpy((char *)client->devid, TEST_DEVID, sizeof(client->devid) - 1);
    strncpy((char *)client->secret_key, TEST_SECRET_KEY, sizeof(client->secret_key) - 1);
    strncpy((char *)client->local_key, TEST_LOCAL_KEY, sizeof(client->local_key) - 1);
    client->mqtt_url = TEST_MQTT_URL;
    client->cacert = g_cacert;
    client->message_callback = test_message_callback;

    int ret = iot_client_message_connect(client);
    if (ret != OPRT_OK) {
        printf("  iot_client_message_connect failed: %d\n", ret);
        pal->free(client);
        return -1;
    }

    wait_for_callback(client, 20);
    reset_callback_state();

    /* Publish plaintext via iot_client_message_publish (encrypts internally) */
    const char *plaintext = "{\"cmd\":\"encrypted_test\"}";
    size_t pt_len = strlen(plaintext);

    ret = iot_client_message_publish(client,
                                     (const uint8_t *)plaintext, pt_len);
    if (ret != OPRT_OK) {
        printf("  iot_client_message_publish failed: %d\n", ret);
        iot_client_message_disconnect(client);
        client->mqtt_url = NULL;
        client->cacert = NULL;
        pal->free(client);
        return -1;
    }

    wait_for_callback(client, 30);

    iot_client_message_disconnect(client);

    int result = -1;
    if (cb_called < 1) {
        printf("  callback not called for encrypted message\n");
    } else if (cb_data_len != pt_len || memcmp(cb_data, plaintext, pt_len) != 0) {
        printf("  decrypted data mismatch: expected '%s', got '%.*s'\n",
               plaintext, (int)cb_data_len, cb_data);
    } else {
        printf("  decrypted: %.*s\n", (int)cb_data_len, cb_data);
        result = 0;
    }

    client->mqtt_url = NULL;
    client->cacert = NULL;
    pal->free(client);
    return result;
}

/* ---------- Test 5: iot_client_init with mqtt_auto_connect=true must short-circuit when no mqtt_url ---------- */

static int test_iot_client_init_autoconnect_no_url(void)
{
    /* Empty devid skips DNS resolution, so mqtt_url stays NULL. The new
     * auto-connect branch (`client->mqtt_url && config->mqtt_auto_connect`)
     * must safely short-circuit — returning a usable, disconnected client
     * instead of crashing or attempting a connect with NULL URL. */
    iot_client_config_t cfg = {0};
    cfg.mqtt_auto_connect = true;

    iot_client_t *client = iot_client_init(&cfg);
    if (!client) {
        printf("  iot_client_init returned NULL\n");
        return -1;
    }

    int result = 0;
    if (client->mqtt_url != NULL) {
        printf("  expected mqtt_url=NULL (no devid → DNS skipped), got '%s'\n", client->mqtt_url);
        result = -1;
    }
    if (client->mqtt != NULL) {
        printf("  expected mqtt=NULL (auto-connect must short-circuit on NULL URL)\n");
        result = -1;
    }
    iot_client_deinit(client);
    return result;
}

/* ---------- Test 6: iot_client_init with mqtt_auto_connect=false also stays disconnected ---------- */

static int test_iot_client_init_no_autoconnect(void)
{
    iot_client_config_t cfg = {0};
    cfg.mqtt_auto_connect = false;

    iot_client_t *client = iot_client_init(&cfg);
    if (!client) {
        printf("  iot_client_init returned NULL\n");
        return -1;
    }

    int result = 0;
    if (client->mqtt != NULL) {
        printf("  expected mqtt=NULL (auto_connect disabled)\n");
        result = -1;
    }
    iot_client_deinit(client);
    return result;
}

/* ---------- main ---------- */

int main(void)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("========== IoT Client Message Test Suite ==========\n");

    const pal_t *pal = get_default_pal();
    iot_init(pal);

    g_cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    if (!g_cacert) {
        fprintf(stderr, "Warning: CA cert not loaded, TLS tests may fail\n");
    }

    if (start_mock() != 0) {
        fprintf(stderr, "Failed to start message mock\n");
        return 1;
    }
    if (start_mock_invalid_format() != 0) {
        fprintf(stderr, "Failed to start message mock (invalid format)\n");
        stop_mock();
        return 1;
    }
    if (start_mock_wrong_key() != 0) {
        fprintf(stderr, "Failed to start message mock (wrong key)\n");
        stop_mock_invalid();
        stop_mock();
        return 1;
    }

    /* Success tests */
    RUN_TEST(test_raw_message);
    RUN_TEST(test_encrypted_message);

    /* Failure tests */
    RUN_TEST(test_invalid_format_message);
    RUN_TEST(test_decrypt_fail_wrong_key);

    /* iot_client_init + mqtt_auto_connect (no mocks needed: empty devid skips DNS) */
    RUN_TEST(test_iot_client_init_autoconnect_no_url);
    RUN_TEST(test_iot_client_init_no_autoconnect);

    stop_mock_wrongkey();
    stop_mock_invalid();
    stop_mock();
    pal->free(g_cacert);

    printf("\n========== Results: %d/%d passed ==========\n",
           tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
