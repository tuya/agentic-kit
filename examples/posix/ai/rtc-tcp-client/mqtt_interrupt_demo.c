/*
 * mqtt_interrupt_demo.c -- Independent MQTT control plus pressured TAI media.
 *
 * This demo uses a bounded synthetic playback queue. MQTT is pumped by one
 * application-owned thread while TAI's worker independently receives media.
 * Interruptions discard audio whose stream started at or before the server's
 * cutoff time, reopening admission so retained stale media can be drained.
 */

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "demo_json.h"
#include "iot_client.h"
#include "tuya_ai.h"

extern const pal_t *tai_pal_posix(void);

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"
#define PLAYBACK_CAPACITY  4
#define MAX_EVENT_ID       64

#define WAIT_STEP_MS 50U
#define WAIT_LIMIT_MS 60000U

typedef struct {
    pthread_mutex_t mutex;
    iot_client_t *iot;
    tai_ctx_t *tai;
    int mqtt_running;
    int mqtt_failed;
    int done;
    int playback_running;
    size_t queued_audio;
    uint64_t audio_cutoff_ms;
    uint64_t stream_start_ms;
    uint64_t queued_start_ms[PLAYBACK_CAPACITY];
    char current_event_id[MAX_EVENT_ID];
    int current_event_has_audio;
} demo_state_t;

#define MAX_AUDIO_TIME_MS ((UINT64_C(1) << 42) - 1)

static uint64_t interrupt_time(const char *data, size_t len, int chat_break)
{
    if (!data || !len) return 0;
    /* cJSON strings have no length: reject encoded NUL rather than silently
     * treating a timestamp with a NUL suffix as a shorter decimal string. */
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\0') return 0;
        if (data[i] == '\\' && i + 1 < len) {
            if (len - i >= 6 && memcmp(data + i, "\\u0000", 6) == 0) return 0;
            i++;
        }
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(data, len, &end, 0);
    if (!root) return 0;
    while (end < data + len &&
           (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    cJSON *object = chat_break
        ? cJSON_GetObjectItemCaseSensitive(root, "breakAttributes") : root;
    cJSON *time = cJSON_GetObjectItemCaseSensitive(object, "time");
    uint64_t result = 0;
    if (end == data + len && cJSON_IsObject(object)) {
        if (cJSON_IsString(time)) {
            const char *p = time->valuestring;
            for (; *p; p++) {
                if (*p < '0' || *p > '9' ||
                    result > (MAX_AUDIO_TIME_MS - (unsigned)(*p - '0')) / 10) {
                    result = 0;
                    break;
                }
                result = result * 10 + (unsigned)(*p - '0');
            }
        } else if (cJSON_IsNumber(time) && time->valuedouble > 0 &&
                   time->valuedouble <= (double)MAX_AUDIO_TIME_MS) {
            uint64_t value = (uint64_t)time->valuedouble;
            if ((double)value == time->valuedouble) result = value;
        }
    }
    cJSON_Delete(root);
    return result;
}

static void interrupt_playback(demo_state_t *state, const char *data,
                               size_t len, int chat_break)
{
    uint64_t cutoff = interrupt_time(data, len, chat_break);
    const char *source = chat_break ? "TAI ChatBreak" : "MQTT asrInterrupt";
    pthread_mutex_lock(&state->mutex);
    if (cutoff) {
        if (cutoff > state->audio_cutoff_ms) state->audio_cutoff_ms = cutoff;
    } else if (state->stream_start_ms > state->audio_cutoff_ms) {
        /* Fail closed. An interruption we cannot place on the server timeline
         * must not leave the reply playing over the user, so drop the stream
         * currently in flight: its own START becomes the cutoff (a START equal
         * to the cutoff is discarded), and later streams are still admitted. */
        state->audio_cutoff_ms = state->stream_start_ms;
    }
    size_t kept = 0;
    for (size_t i = 0; i < state->queued_audio; i++) {
        if (state->queued_start_ms[i] > state->audio_cutoff_ms)
            state->queued_start_ms[kept++] = state->queued_start_ms[i];
    }
    state->queued_audio = kept;
    pthread_mutex_unlock(&state->mutex);
    if (cutoff)
        printf("\n[%s] interruption time=%" PRIu64 "\n", source, cutoff);
    else
        printf("\n[%s] no usable interruption time; dropped the in-flight stream\n",
               source);
}

static void on_ai_control(const char *type, const char *json_data,
                          size_t data_len, void *user_data)
{
    if (strcmp(type, "asrInterrupt") != 0) return;
    interrupt_playback((demo_state_t *)user_data, json_data, data_len, 0);
}

static int on_flow_control(tai_ctx_t *ctx, void *user_data)
{
    (void)ctx;
    demo_state_t *state = (demo_state_t *)user_data;
    pthread_mutex_lock(&state->mutex);
    int admit = state->queued_audio < PLAYBACK_CAPACITY;
    pthread_mutex_unlock(&state->mutex);
    return admit;
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *user_data)
{
    (void)ctx;
    demo_state_t *state = (demo_state_t *)user_data;
    pthread_mutex_lock(&state->mutex);
    if (msg->stream_flag == TAI_STREAM_START ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT)
        state->stream_start_ms = msg->timestamp_ms;
    state->current_event_has_audio = 1;
    if (msg->len && state->stream_start_ms > state->audio_cutoff_ms &&
        state->queued_audio < PLAYBACK_CAPACITY)
        state->queued_start_ms[state->queued_audio++] = state->stream_start_ms;
    pthread_mutex_unlock(&state->mutex);
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *user_data)
{
    (void)ctx;
    demo_state_t *state = (demo_state_t *)user_data;
    if (msg->event_type == TAI_EVT_START && msg->event_id && msg->event_id[0]) {
        pthread_mutex_lock(&state->mutex);
        snprintf(state->current_event_id, sizeof(state->current_event_id),
                 "%s", msg->event_id);
        state->current_event_has_audio = 0;
        pthread_mutex_unlock(&state->mutex);
    } else if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        interrupt_playback(state, (const char *)msg->user_data,
                           msg->user_data_len, 1);
    } else if (msg->event_type == TAI_EVT_END) {
        pthread_mutex_lock(&state->mutex);
        if (msg->event_id && state->current_event_id[0] &&
            strcmp(state->current_event_id, msg->event_id) == 0) {
            state->current_event_id[0] = '\0';
            if (!state->current_event_has_audio || !state->audio_cutoff_ms ||
                state->stream_start_ms > state->audio_cutoff_ms)
                state->done = 1;
        }
        pthread_mutex_unlock(&state->mutex);
    }
}

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *user_data)
{
    (void)ctx;
    (void)user_data;
    fwrite(msg->text, 1, msg->len, stdout);
    fflush(stdout);
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg,
                          void *user_data)
{
    (void)ctx;
    demo_state_t *state = (demo_state_t *)user_data;
    fprintf(stderr, "\n[TAI disconnected: reason=%u detail=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->detail);
    pthread_mutex_lock(&state->mutex);
    state->done = 1;
    pthread_mutex_unlock(&state->mutex);
}

static void *mqtt_owner(void *arg)
{
    demo_state_t *state = (demo_state_t *)arg;
    while (1) {
        pthread_mutex_lock(&state->mutex);
        int running = state->mqtt_running;
        pthread_mutex_unlock(&state->mutex);
        if (!running) break;
        if (iot_client_process(state->iot, WAIT_STEP_MS) != OPRT_OK) {
            pthread_mutex_lock(&state->mutex);
            state->mqtt_failed = 1;
            state->mqtt_running = 0;
            pthread_mutex_unlock(&state->mutex);
            break;
        }
    }
    return NULL;
}

static void *playback_consumer(void *arg)
{
    demo_state_t *state = (demo_state_t *)arg;
    while (1) {
        state->iot->pal->sleep_ms(100);
        pthread_mutex_lock(&state->mutex);
        if (!state->playback_running) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }
        if (state->queued_audio > 0) {
            state->queued_audio--;
            memmove(state->queued_start_ms, state->queued_start_ms + 1,
                    state->queued_audio * sizeof(state->queued_start_ms[0]));
        }
        pthread_mutex_unlock(&state->mutex);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *devid = argc > 1 ? argv[1] : DEFAULT_DEVID;
    const char *secret_key = argc > 2 ? argv[2] : DEFAULT_SECRET_KEY;
    const char *local_key = argc > 3 ? argv[3] : DEFAULT_LOCAL_KEY;
    const pal_t *pal = tai_pal_posix();
    demo_state_t state;
    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.mutex, NULL);

    if (iot_init_default() != OPRT_OK) return 1;
    iot_client_config_t iot_cfg = {
        .region = AY,
        .env = PROD,
        .mqtt_disable_auto_connect = true,
    };
    if (demo_copy_field(iot_cfg.devid, sizeof(iot_cfg.devid), devid, "devid") ||
        demo_copy_field(iot_cfg.secret_key, sizeof(iot_cfg.secret_key),
                        secret_key, "secret_key") ||
        demo_copy_field(iot_cfg.local_key, sizeof(iot_cfg.local_key),
                        local_key, "local_key")) {
        return 1;
    }
    state.iot = iot_client_init(&iot_cfg);
    if (!state.iot) return 1;

    char *token = (char *)calloc(1, 4096);
    tai_conn_params_t params;
    if (!token || iot_client_get_session_token(state.iot, NULL, token, 4096) ||
        parse_token(token, &params)) {
        fprintf(stderr, "failed to obtain/parse TAI session token\n");
        free(token);
        iot_client_deinit(state.iot);
        return 1;
    }
    if (params.biz_code == 0) params.biz_code = 65537;
    if (params.biz_tag == 0) params.biz_tag = 119;

    iot_ai_ctrl_set_callback(state.iot, on_ai_control, &state);
    if (iot_client_connect(state.iot) != OPRT_OK) {
        fprintf(stderr, "MQTT connect failed\n");
        free(token);
        iot_client_deinit(state.iot);
        return 1;
    }

    tai_config_t tai_cfg = {
        .host = params.host,
        .port = params.port,
        .tls_sni = params.tls_sni,
        .device_id = params.derived_client_id,
        .local_key = local_key,
        .client_type = TAI_CLIENT_DEVICE,
        .protocol_version = TAI_VER_21,
        .sign_level = TAI_SIGN_HMAC_SHA256,
        .biz_code = (uint32_t)params.biz_code,
        .biz_tag = (uint64_t)params.biz_tag,
        .agent_token = params.agent_token,
        .pal = pal,
        .on_audio = on_audio,
        .on_text = on_text,
        .on_event = on_event,
        .on_disconnect = on_disconnect,
        .on_flow_control = on_flow_control,
        .user_data = &state,
    };
    void *tai_mem = pal->malloc(tai_ctx_size());
    state.tai = tai_mem ? tai_ctx_init(tai_mem, &tai_cfg) : NULL;
    if (!state.tai || tai_connect(state.tai) != TAI_OK) {
        fprintf(stderr, "TAI connect failed\n");
        if (state.tai) tai_ctx_deinit(state.tai);
        pal->free(tai_mem);
        free(token);
        iot_client_deinit(state.iot);
        return 1;
    }

    pthread_t mqtt_thread;
    pthread_t playback_thread;
    state.mqtt_running = 1;
    state.playback_running = 1;
    pthread_create(&mqtt_thread, NULL, mqtt_owner, &state);
    pthread_create(&playback_thread, NULL, playback_consumer, &state);

    const char *question = "Please give a detailed explanation of TCP backpressure.";
    int send_rc = tai_send_text(state.tai, question, strlen(question));
    uint32_t waited = 0;
    while (send_rc == TAI_OK && waited < WAIT_LIMIT_MS) {
        pal->sleep_ms(WAIT_STEP_MS);
        pthread_mutex_lock(&state.mutex);
        int done = state.done || state.mqtt_failed;
        pthread_mutex_unlock(&state.mutex);
        if (done) break;
        waited += WAIT_STEP_MS;
    }

    pthread_mutex_lock(&state.mutex);
    state.mqtt_running = 0;
    state.playback_running = 0;
    pthread_mutex_unlock(&state.mutex);
    pthread_join(mqtt_thread, NULL);
    pthread_join(playback_thread, NULL);
    tai_disconnect(state.tai);
    tai_ctx_deinit(state.tai);
    pal->free(tai_mem);
    free(token);
    iot_client_deinit(state.iot);
    pthread_mutex_destroy(&state.mutex);
    return send_rc == TAI_OK && !state.mqtt_failed ? 0 : 1;
}
