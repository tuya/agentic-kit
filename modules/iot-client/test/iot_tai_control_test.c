/*
 * Cross-module regression: MQTT control remains deliverable while TAI receive
 * backpressure stops the media Connection, then stale media drains without
 * refilling playback and a new Event remains playable.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "iot_client.h"
#include "iot_client_message.h"
#include "tai_internal.h"
#include "tai_pal_loopback.h"

#define main combined_demo_main
#include "../../../examples/posix/ai/rtc-tcp-client/mqtt_interrupt_demo.c"
#undef main

#define TEST_DEVID      "test_device_msg_001"
#define TEST_SECRET_KEY "abcdef1234567890"
#define TEST_LOCAL_KEY  "0123456789abcdef"
#define TEST_MQTT_URL   "mqtts://127.0.0.1:11885"
#define TEST_MQTT_PORT  11885
#define WRONG_KEY_PORT  11887

#define EARLY_AUDIO_MS UINT64_C(1700000000000)
#define OLD_AUDIO_MS   UINT64_C(1700000002000)
#define NEW_AUDIO_MS   UINT64_C(1700000004000)

extern const pal_t *tai_pal_posix(void);
int mqtt_interrupt_demo_tests(void);

static int failures;
static const pal_t *posix_pal;
static const pal_t *loopback_pal;
static pal_t routing_pal;

static int is_loopback_handle(void *tcp)
{
    return tcp == (void *)0x1;
}

static void *routing_tcp_connect(const char *host, uint16_t port,
                                 uint32_t timeout_ms)
{
    if (host && strcmp(host, "loopback.test") == 0) {
        return loopback_pal->tcp_connect(host, port, timeout_ms);
    }
    return posix_pal->tcp_connect(host, port, timeout_ms);
}

static int routing_tcp_send(void *tcp, const uint8_t *buf, size_t len,
                            uint32_t timeout_ms)
{
    const pal_t *owner = is_loopback_handle(tcp) ? loopback_pal : posix_pal;
    return owner->tcp_send(tcp, buf, len, timeout_ms);
}

static int routing_tcp_recv(void *tcp, uint8_t *buf, size_t len,
                            uint32_t timeout_ms)
{
    const pal_t *owner = is_loopback_handle(tcp) ? loopback_pal : posix_pal;
    return owner->tcp_recv(tcp, buf, len, timeout_ms);
}

static void routing_tcp_close(void *tcp)
{
    const pal_t *owner = is_loopback_handle(tcp) ? loopback_pal : posix_pal;
    owner->tcp_close(tcp);
}

static int routing_tcp_poll(void *tcp, int events, uint32_t timeout_ms)
{
    const pal_t *owner = is_loopback_handle(tcp) ? loopback_pal : posix_pal;
    return owner->tcp_poll(tcp, events, timeout_ms);
}

static const pal_t *make_routing_pal(void)
{
    posix_pal = tai_pal_posix();
    loopback_pal = tai_pal_loopback();
    routing_pal = *posix_pal;
    routing_pal.tcp_connect = routing_tcp_connect;
    routing_pal.tcp_send = routing_tcp_send;
    routing_pal.tcp_recv = routing_tcp_recv;
    routing_pal.tcp_close = routing_tcp_close;
    routing_pal.tcp_poll = routing_tcp_poll;
    return &routing_pal;
}


#define CHECK(expr)                                                       \
    do {                                                                  \
        if (!(expr)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void sleep_ms(uint32_t ms)
{
    struct timespec ts = { ms / 1000U, (long)(ms % 1000U) * 1000000L };
    nanosleep(&ts, NULL);
}

typedef struct {
    /* This mutex serializes counters and wrappers; callbacks also take playback.mutex. */
    pthread_mutex_t mutex;
    demo_state_t playback;
    int audio_callbacks;
    int accepted_old_audio;
    int accepted_new_audio;
    int accepted_early_audio;
    int chat_breaks;
    int mqtt_interrupts;
    int interrupted_before_tcp_drain;
    pthread_t mqtt_callback_thread;
} app_state_t;

static app_state_t state;
static pid_t mqtt_mock_pid = -1;
static pid_t wrong_key_mock_pid = -1;

static char *load_file(const pal_t *pal, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = (char *)pal->malloc((size_t)len + 1);
    if (data && fread(data, 1, (size_t)len, file) == (size_t)len) {
        data[len] = '\0';
    } else {
        pal->free(data);
        data = NULL;
    }
    fclose(file);
    return data;
}

static int wait_for_port(uint16_t port)
{
    for (int attempt = 0; attempt < 100; attempt++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in address = {0};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
        close(fd);
        if (result == 0) return 0;
        sleep_ms(20);
    }
    return -1;
}

static int start_mqtt_mock(void)
{
    mqtt_mock_pid = fork();
    if (mqtt_mock_pid == 0) {
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        _exit(1);
    }
    if (mqtt_mock_pid < 0) return -1;

    wrong_key_mock_pid = fork();
    if (wrong_key_mock_pid == 0) {
        setenv("MESSAGE_MOCK_TYPE", "wrong_key_encrypted", 1);
        setenv("MESSAGE_MOCK_PORT", "11887", 1);
        execlp(PYTHON3_EXEC, PYTHON3_EXEC, MESSAGE_MOCK_PATH, NULL);
        _exit(1);
    }
    if (wrong_key_mock_pid < 0) return -1;
    return wait_for_port(TEST_MQTT_PORT) || wait_for_port(WRONG_KEY_PORT);
}

static void stop_mqtt_mock(void)
{
    if (wrong_key_mock_pid > 0) {
        kill(wrong_key_mock_pid, SIGTERM);
        waitpid(wrong_key_mock_pid, NULL, 0);
        wrong_key_mock_pid = -1;
    }
    if (mqtt_mock_pid > 0) {
        kill(mqtt_mock_pid, SIGTERM);
        waitpid(mqtt_mock_pid, NULL, 0);
        mqtt_mock_pid = -1;
    }
}

#define WAIT_FOR(expr, timeout_ms)                                        \
    ({                                                                    \
        int found = 0;                                                    \
        for (uint32_t waited = 0; waited < (timeout_ms); waited += 5) {   \
            pthread_mutex_lock(&state.mutex);                             \
            found = (expr);                                               \
            pthread_mutex_unlock(&state.mutex);                           \
            if (found) break;                                             \
            sleep_ms(5);                                                  \
        }                                                                 \
        found;                                                            \
    })

#define STATE_CHECK(expr)                                  \
    do {                                                  \
        pthread_mutex_lock(&state.mutex);                  \
        CHECK(expr);                                      \
        pthread_mutex_unlock(&state.mutex);                \
    } while (0)

static int flow_control(tai_ctx_t *ctx, void *user_data)
{
    (void)ctx;
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    /* One queued frame is enough to pressure the real worker in this test. */
    int admit = s->playback.queued_audio == 0;
    pthread_mutex_unlock(&s->mutex);
    return admit;
}

static void combined_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg,
                           void *user_data)
{
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    s->audio_callbacks++;
    size_t before = s->playback.queued_audio;
    on_audio(ctx, msg, &s->playback);
    if (s->playback.queued_audio > before) {
        if (msg->event_id && strcmp(msg->event_id, "old-event") == 0) {
            s->accepted_old_audio++;
        } else if (msg->event_id && strcmp(msg->event_id, "new-event") == 0) {
            s->accepted_new_audio++;
        } else if (msg->event_id && strcmp(msg->event_id, "early-event") == 0) {
            s->accepted_early_audio++;
        }
    }
    pthread_mutex_unlock(&s->mutex);
}

static void combined_event(tai_ctx_t *ctx, const tai_event_msg_t *msg,
                           void *user_data)
{
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    if (msg->event_type == TAI_EVT_CHAT_BREAK) s->chat_breaks++;
    on_event(ctx, msg, &s->playback);
    pthread_mutex_unlock(&s->mutex);
}

static void combined_control(const char *type, const char *json_data,
                             size_t data_len, void *user_data)
{
    app_state_t *s = (app_state_t *)user_data;
    if (strcmp(type, "asrInterrupt") != 0) return;
    pthread_mutex_lock(&s->mutex);
    s->mqtt_callback_thread = pthread_self();
    s->mqtt_interrupts++;
    if (s->mqtt_interrupts == 2 && s->chat_breaks == 0)
        s->interrupted_before_tcp_drain = 1;
    on_ai_control(type, json_data, data_len, &s->playback);
    pthread_mutex_unlock(&s->mutex);
}

static int server_push(tai_ctx_t *ctx, uint8_t packet_type,
                       const tai_attr_t *attrs, int attr_count,
                       const uint8_t *payload, size_t payload_len,
                       uint16_t sequence)
{
    uint8_t packet[512];
    uint8_t frame[600];
    int packet_len = tai_packet_encode(TAI_VER_21, packet_type,
                                       attrs, attr_count,
                                       payload, payload_len,
                                       packet, sizeof(packet));
    if (packet_len <= 0) return packet_len;
    int frame_len = tai_frame_encode(TAI_FRAG_NONE, sequence,
                                     packet, (size_t)packet_len,
                                     ctx->sign_key, 32, ctx->pal,
                                     frame, sizeof(frame));
    if (frame_len > 0) tai_loopback_push_recv(frame, (size_t)frame_len);
    return frame_len;
}

static int server_push_event(tai_ctx_t *ctx, uint16_t event_type,
                             const char *event_id, uint16_t sequence)
{
    tai_attr_t attrs[3] = {
        tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id),
        tai_attr_strv(TAI_ATTR_EVENT_ID, event_id),
        tai_attr_strv(TAI_ATTR_USER_DATA,
                      "{\"breakAttributes\":{\"time\":\"1700000003000\"}}"),
    };
    uint8_t payload[4];
    int payload_len = tai_pack_event(TAI_VER_21, event_type,
                                     NULL, 0, payload, sizeof(payload));
    if (payload_len <= 0) return payload_len;
    return server_push(ctx, TAI_PKT_EVENT, attrs,
                       event_type == TAI_EVT_CHAT_BREAK ? 3 : 2,
                       payload, (size_t)payload_len, sequence);
}

static int server_push_audio(tai_ctx_t *ctx, const char *event_id,
                             uint64_t timestamp_ms, uint8_t fill, uint16_t sequence)
{
    uint8_t payload[48];
    int header_len = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                        TAI_STREAM_START,
                                        timestamp_ms,
                                        payload, sizeof(payload));
    if (header_len <= 0) return header_len;
    memset(payload + header_len, fill, 40);

    tai_attr_t attrs[2] = {
        tai_attr_strv(TAI_ATTR_EVENT_ID, event_id),
        tai_attr_strv(TAI_ATTR_AUDIO_PARAMS,
                      "111 1 16 16000 0 16000 20 40"),
    };
    return server_push(ctx, TAI_PKT_AUDIO, attrs, 2,
                       payload, (size_t)header_len + 40, sequence);
}

int main(void)
{
    const pal_t *pal = make_routing_pal();
    pthread_mutex_init(&state.mutex, NULL);
    pthread_mutex_init(&state.playback.mutex, NULL);
    CHECK(iot_init(pal) == OPRT_OK);
    failures += mqtt_interrupt_demo_tests();
    CHECK(start_mqtt_mock() == 0);

    char *cacert = load_file(pal, TEST_CONFIG_DIR "/root_cert.pem");
    CHECK(cacert != NULL);
    iot_client_t iot;
    memset(&iot, 0, sizeof(iot));
    iot.pal = pal;
    snprintf(iot.devid, sizeof(iot.devid), "%s", TEST_DEVID);
    snprintf(iot.secret_key, sizeof(iot.secret_key), "%s", TEST_SECRET_KEY);
    snprintf(iot.local_key, sizeof(iot.local_key), "%s", TEST_LOCAL_KEY);
    snprintf(iot.mqtt_url, sizeof(iot.mqtt_url), "%s", TEST_MQTT_URL);
    iot.cacert = cacert;
    CHECK(iot_ai_ctrl_set_callback(&iot, combined_control, &state) == OPRT_OK);
    CHECK(iot_client_message_connect(&iot) == OPRT_OK);
    /* Consume the mock's initial raw message before the combined scenario. */
    CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);

    /* An authenticated-looking payload encrypted with another local key must
     * never reach the control callback. */
    iot_client_t wrong_key_iot = iot;
    wrong_key_iot.mqtt = NULL;
    snprintf(wrong_key_iot.mqtt_url, sizeof(wrong_key_iot.mqtt_url),
             "mqtts://127.0.0.1:%u", WRONG_KEY_PORT);
    CHECK(iot_client_message_connect(&wrong_key_iot) == OPRT_OK);
    CHECK(iot_client_message_process(&wrong_key_iot, 50) == OPRT_OK);
    STATE_CHECK(state.mqtt_interrupts == 0);
    iot_client_message_disconnect(&wrong_key_iot);

    tai_loopback_reset();
    tai_loopback_set_local_key("test-local-key-16");

    tai_config_t config = {
        .host = "loopback.test",
        .port = 443,
        .device_id = "test-device",
        .local_key = "test-local-key-16",
        .protocol_version = TAI_VER_21,
        .client_type = TAI_CLIENT_DEVICE,
        .sign_level = TAI_SIGN_HMAC_SHA256,
        .disable_tls = 1,
        .pal = pal,
        .on_audio = combined_audio,
        .on_event = combined_event,
        .on_flow_control = flow_control,
        .user_data = &state,
    };
    static uint8_t context_memory[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = tai_ctx_init(context_memory, &config);
    CHECK(ctx != NULL);
    if (!ctx) return 1;
    CHECK(tai_connect(ctx) == TAI_OK);

    uint8_t sent[2048];
    (void)tai_loopback_pop_sent(sent, sizeof(sent));
    uint16_t sequence = 10;
    static const char early_control[] =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\","
        "\"data\":{\"time\":\"1700000001000\"}}}}";
    CHECK(iot_client_message_publish(&iot, (const uint8_t *)early_control,
                                     sizeof(early_control) - 1) == OPRT_OK);
    for (int attempt = 0; attempt < 50 && !WAIT_FOR(state.mqtt_interrupts >= 1, 5); attempt++) {
        CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);
    }
    STATE_CHECK(state.mqtt_interrupts == 1);
    STATE_CHECK(state.playback.queued_audio == 0);
    /* MQTT-before-audio ordering: early-event is cancelled before its START;
     * its later audio must drain without entering playback. */
    CHECK(server_push_event(ctx, TAI_EVT_START, "early-event", sequence++) > 0);
    CHECK(server_push_audio(ctx, "early-event", EARLY_AUDIO_MS, 0x01, sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_END, "early-event", sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_START, "old-event", sequence++) > 0);
    CHECK(server_push_audio(ctx, "old-event", OLD_AUDIO_MS, 0x02, sequence++) > 0);
    CHECK(WAIT_FOR(state.accepted_old_audio == 1, 1000));
    STATE_CHECK(state.accepted_early_audio == 0);
    STATE_CHECK(state.playback.queued_audio == 1);

    pthread_mutex_lock(&state.mutex);
    state.playback.queued_audio = 0;
    pthread_mutex_unlock(&state.mutex);
    CHECK(server_push_audio(ctx, "old-event", OLD_AUDIO_MS, 0x11, sequence++) > 0);
    CHECK(WAIT_FOR(state.accepted_old_audio == 2, 1000));

    CHECK(server_push_audio(ctx, "old-event", OLD_AUDIO_MS, 0x22, sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_CHAT_BREAK,
                            "old-event", sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_END, "old-event", sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_START, "new-event", sequence++) > 0);
    CHECK(server_push_audio(ctx, "new-event", NEW_AUDIO_MS, 0x33, sequence++) > 0);
    /* Keep admission closed while the delayed duplicate waits on TCP. */
    CHECK(server_push_event(ctx, TAI_EVT_CHAT_BREAK,
                            "old-event", sequence++) > 0);
    sleep_ms(100);
    STATE_CHECK(state.audio_callbacks == 3);
    STATE_CHECK(state.chat_breaks == 0);

    static const char control[] =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\","
        "\"data\":{\"time\":\"1700000003000\"}}}}";
    pthread_t mqtt_owner = pthread_self();
    CHECK(iot_client_message_publish(&iot, (const uint8_t *)control,
                                     sizeof(control) - 1) == OPRT_OK);
    for (int attempt = 0; attempt < 50 && !WAIT_FOR(state.mqtt_interrupts >= 2, 5); attempt++) {
        CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);
    }
    STATE_CHECK(state.mqtt_interrupts == 2);
    pthread_mutex_lock(&state.mutex);
    int same_thread = pthread_equal(mqtt_owner, state.mqtt_callback_thread);
    pthread_mutex_unlock(&state.mutex);
    CHECK(same_thread);
    STATE_CHECK(state.interrupted_before_tcp_drain == 1);

    CHECK(WAIT_FOR(state.accepted_new_audio == 1, 2000));
    STATE_CHECK(state.audio_callbacks == 5);
    STATE_CHECK(state.accepted_old_audio == 2);
    STATE_CHECK(state.accepted_new_audio == 1);
    STATE_CHECK(state.chat_breaks == 1);
    STATE_CHECK(state.playback.queued_audio == 1);

    /* Release pressure by consuming new audio; the queued duplicate is ignored. */
    pthread_mutex_lock(&state.mutex);
    state.playback.queued_audio = 0;
    pthread_mutex_unlock(&state.mutex);
    CHECK(WAIT_FOR(state.chat_breaks == 2, 2000));
    STATE_CHECK(state.playback.queued_audio == 0);
    STATE_CHECK(state.accepted_new_audio == 1);

    /* Delayed duplicate on MQTT must also not flush the new playback. */
    pthread_mutex_lock(&state.mutex);
    state.playback.queued_audio = 1;
    pthread_mutex_unlock(&state.mutex);
    CHECK(iot_client_message_publish(&iot, (const uint8_t *)control,
                                     sizeof(control) - 1) == OPRT_OK);
    for (int attempt = 0; attempt < 50 && !WAIT_FOR(state.mqtt_interrupts >= 3, 5); attempt++) {
        CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);
    }
    STATE_CHECK(state.mqtt_interrupts == 3);
    STATE_CHECK(state.playback.queued_audio == 1);
    STATE_CHECK(state.accepted_new_audio == 1);

    /* MQTT loss removes only the independent control path. TAI stays paused on
     * the full playback queue; reconnect restores authenticated delivery. */
    iot_client_message_disconnect(&iot);
    CHECK(iot_client_message_publish(&iot, (const uint8_t *)control,
                                     sizeof(control) - 1) == OPRT_UNINITIALIZED);
    STATE_CHECK(state.playback.queued_audio == 1);
    CHECK(iot_client_message_connect(&iot) == OPRT_OK);
    CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);
    CHECK(iot_client_message_publish(&iot, (const uint8_t *)control,
                                     sizeof(control) - 1) == OPRT_OK);
    for (int attempt = 0; attempt < 50 && !WAIT_FOR(state.mqtt_interrupts >= 4, 5); attempt++) {
        CHECK(iot_client_message_process(&iot, 50) == OPRT_OK);
    }
    STATE_CHECK(state.mqtt_interrupts == 4);
    STATE_CHECK(state.playback.queued_audio == 1);

    /* Shutdown while receive admission is still closed must not hang. */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    iot_client_message_disconnect(&iot);
    iot.cacert = NULL;
    pal->free(cacert);
    stop_mqtt_mock();
    pthread_mutex_destroy(&state.playback.mutex);
    pthread_mutex_destroy(&state.mutex);
    printf("iot_tai_control_test: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
