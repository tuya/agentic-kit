/*
 * Cross-module regression: MQTT control remains deliverable while TAI receive
 * backpressure stops the media Connection, then stale media drains without
 * refilling playback and a new Event remains playable.
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "iot_ai_ctrl.h"
#include "iot_client.h"
#include "tai_internal.h"
#include "tai_pal_loopback.h"

static int failures;

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
    pthread_mutex_t mutex;
    size_t queued_audio;
    int audio_callbacks;
    int accepted_old_audio;
    int accepted_new_audio;
    int chat_breaks;
    int mqtt_interrupts;
    char stale_event_id[64];
} app_state_t;

static app_state_t state;

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

static int flow_control(tai_ctx_t *ctx, void *user_data)
{
    (void)ctx;
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    int admit = s->queued_audio == 0;
    pthread_mutex_unlock(&s->mutex);
    return admit;
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg,
                     void *user_data)
{
    (void)ctx;
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    s->audio_callbacks++;
    int stale = s->stale_event_id[0] && msg->event_id &&
                strcmp(s->stale_event_id, msg->event_id) == 0;
    if (!stale && s->queued_audio == 0) {
        s->queued_audio = 1;
        if (msg->event_id && strcmp(msg->event_id, "old-event") == 0) {
            s->accepted_old_audio++;
        } else if (msg->event_id && strcmp(msg->event_id, "new-event") == 0) {
            s->accepted_new_audio++;
        }
    }
    pthread_mutex_unlock(&s->mutex);
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg,
                     void *user_data)
{
    (void)ctx;
    app_state_t *s = (app_state_t *)user_data;
    pthread_mutex_lock(&s->mutex);
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        s->chat_breaks++;
        s->queued_audio = 0;
        if (msg->event_id && msg->event_id[0]) {
            snprintf(s->stale_event_id, sizeof(s->stale_event_id),
                     "%s", msg->event_id);
        }
    } else if (msg->event_type == TAI_EVT_START &&
               s->stale_event_id[0] && msg->event_id && msg->event_id[0] &&
               strcmp(s->stale_event_id, msg->event_id) != 0) {
        s->stale_event_id[0] = '\0';
    }
    pthread_mutex_unlock(&s->mutex);
}

static void on_ai_control(const char *type, const char *json_data,
                          size_t data_len, void *user_data)
{
    app_state_t *s = (app_state_t *)user_data;
    if (strcmp(type, "asrInterrupt") != 0) return;

    static const char expected[] = "\"eventId\":\"old-event\"";
    int matched = 0;
    for (size_t i = 0; i + sizeof(expected) - 1 <= data_len; i++) {
        if (memcmp(json_data + i, expected, sizeof(expected) - 1) == 0) {
            matched = 1;
            break;
        }
    }
    if (!matched) return;

    pthread_mutex_lock(&s->mutex);
    s->mqtt_interrupts++;
    s->queued_audio = 0;
    snprintf(s->stale_event_id, sizeof(s->stale_event_id), "%s", "old-event");
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
    tai_attr_t attrs[2] = {
        tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id),
        tai_attr_strv(TAI_ATTR_EVENT_ID, event_id),
    };
    uint8_t payload[4];
    int payload_len = tai_pack_event(TAI_VER_21, event_type,
                                     NULL, 0, payload, sizeof(payload));
    if (payload_len <= 0) return payload_len;
    return server_push(ctx, TAI_PKT_EVENT, attrs, 2,
                       payload, (size_t)payload_len, sequence);
}

static int server_push_audio(tai_ctx_t *ctx, const char *event_id,
                             uint8_t fill, uint16_t sequence)
{
    uint8_t payload[48];
    int header_len = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                        TAI_STREAM_START,
                                        ctx->pal->time_ms(),
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
    const pal_t *pal = tai_pal_loopback();
    pthread_mutex_init(&state.mutex, NULL);
    tai_loopback_reset();
    tai_loopback_set_local_key("test-local-key-16");
    CHECK(iot_init(pal) == OPRT_OK);

    iot_client_t iot;
    memset(&iot, 0, sizeof(iot));
    iot.pal = pal;
    CHECK(iot_ai_ctrl_set_callback(&iot, on_ai_control, &state) == OPRT_OK);

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
        .on_audio = on_audio,
        .on_event = on_event,
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
    CHECK(server_push_event(ctx, TAI_EVT_START, "old-event", sequence++) > 0);
    CHECK(server_push_audio(ctx, "old-event", 0x11, sequence++) > 0);
    CHECK(WAIT_FOR(state.accepted_old_audio == 1, 1000));

    CHECK(server_push_audio(ctx, "old-event", 0x22, sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_CHAT_BREAK,
                            "old-event", sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_END, "old-event", sequence++) > 0);
    CHECK(server_push_event(ctx, TAI_EVT_START, "new-event", sequence++) > 0);
    CHECK(server_push_audio(ctx, "new-event", 0x33, sequence++) > 0);
    sleep_ms(100);
    CHECK(state.audio_callbacks == 1);
    CHECK(state.chat_breaks == 0);

    static const char control[] =
        "{\"protocol\":9000,\"data\":{\"data\":{"
        "\"type\":\"asrInterrupt\","
        "\"data\":{\"eventId\":\"old-event\"}}}}";
    CHECK(iot_ai_ctrl_dispatch(&iot, (const uint8_t *)control,
                               sizeof(control) - 1));
    CHECK(state.mqtt_interrupts == 1);
    CHECK(state.chat_breaks == 0);

    CHECK(WAIT_FOR(state.accepted_new_audio == 1, 2000));
    CHECK(state.audio_callbacks == 3);
    CHECK(state.accepted_old_audio == 1);
    CHECK(state.accepted_new_audio == 1);
    CHECK(state.chat_breaks == 1);
    CHECK(state.queued_audio == 1);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pthread_mutex_destroy(&state.mutex);
    printf("iot_tai_control_test: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
