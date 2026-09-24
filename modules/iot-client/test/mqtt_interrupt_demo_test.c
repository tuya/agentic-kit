/* Include the demo so regressions exercise its actual static callbacks rather
 * than a test-only copy of the interruption/filtering logic. Never run its main. */
#define main mqtt_interrupt_demo_main
#include "../../../examples/posix/ai/rtc-tcp-client/mqtt_interrupt_demo.c"
#undef main

#include "tai_internal.h"

#define DEMO_CHECK(expr)                                                  \
    do {                                                                  \
        if (!(expr)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define EPOCH_MS UINT64_C(1700000000000)

static int demo_test_event(tai_ctx_t *ctx, uint16_t type, const char *event_id,
                           const char *user_data, const char *body)
{
    uint8_t payload[256];
    tai_attr_t attrs[2] = {
        tai_attr_strv(TAI_ATTR_EVENT_ID, event_id),
        tai_attr_strv(TAI_ATTR_USER_DATA, user_data ? user_data : ""),
    };
    int len = tai_pack_event(TAI_VER_21, type, (const uint8_t *)body,
                             body ? strlen(body) : 0, payload, sizeof(payload));
    if (len <= 0) return TAI_ERR_PROTO;
    return tai_proto_dispatch(ctx, TAI_PKT_EVENT, attrs, user_data ? 2 : 1,
                              payload, (size_t)len);
}

static int demo_test_audio(tai_ctx_t *ctx, uint8_t flag, uint64_t timestamp,
                           size_t frames)
{
    /* Keep the paused body's storage pinned in rx_buf, just like the worker. */
    int len = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                 flag, timestamp, ctx->rx_buf, sizeof(ctx->rx_buf));
    if (len <= 0) return TAI_ERR_PROTO;
    size_t body_len = frames * 40;
    if (body_len > sizeof(ctx->rx_buf) - (size_t)len) return TAI_ERR_MEM;
    for (size_t i = 0; i < frames; i++)
        memset(ctx->rx_buf + len + i * 40, (int)i + 1, 40);
    tai_attr_t attr = tai_attr_strv(TAI_ATTR_AUDIO_PARAMS,
                                    "111 1 16 16000 0 16000 20 40");
    return tai_proto_dispatch(ctx, TAI_PKT_AUDIO, &attr, 1,
                              ctx->rx_buf, (size_t)len + body_len);
}

static void demo_test_interrupt(demo_state_t *state, uint64_t timestamp)
{
    char json[64];
    int len = snprintf(json, sizeof(json), "{\"time\":\"%" PRIu64 "\"}", timestamp);
    on_ai_control("asrInterrupt", json, (size_t)len, state);
}

static void *demo_racing_audio(void *arg)
{
    demo_state_t *state = arg;
    tai_audio_msg_t audio = {
        .stream_flag = TAI_STREAM_START,
        .data = (const uint8_t *)"opus",
        .len = 4,
    };
    for (uint64_t i = 1; i <= 1000; i++) {
        audio.timestamp_ms = EPOCH_MS + i;
        if (on_flow_control(NULL, state)) on_audio(NULL, &audio, state);
    }
    return NULL;
}

static void *demo_racing_interrupt(void *arg)
{
    demo_state_t *state = arg;
    static const char cutoff[] = "{\"time\":\"1700000001000\"}";
    on_ai_control("asrInterrupt", cutoff, sizeof(cutoff) - 1, state);
    return NULL;
}

int mqtt_interrupt_demo_tests(void)
{
    int failures = 0;
    const pal_t *pal = tai_pal_posix();
    demo_state_t state = {0};
    if (pthread_mutex_init(&state.mutex, NULL) != 0) {
        fprintf(stderr, "FAIL mqtt_interrupt_demo_tests: mutex init\n");
        return 1;
    }
    tai_config_t config = {
        .pal = pal,
        .protocol_version = TAI_VER_21,
        .on_audio = on_audio,
        .on_event = on_event,
        .on_flow_control = on_flow_control,
        .user_data = &state,
    };
    void *memory = pal->malloc(tai_ctx_size());
    state.tai = memory ? tai_ctx_init(memory, &config) : NULL;
    DEMO_CHECK(state.tai != NULL);
    if (!state.tai) goto cleanup;
    tai_ctx_t *ctx = state.tai;

    /* The two Event slices are independent. A plausible payload is never a
     * fallback for a missing or malformed Attribute 111: an unusable time
     * fails closed onto the in-flight stream's own START (EPOCH_MS), never
     * onto the payload's timestamp (+900). */
    const char *plausible_body =
        "{\"breakAttributes\":{\"time\":\"1700000000900\"}}";
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS, 1) == TAI_OK);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated", NULL,
                               plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS && state.queued_audio == 0);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated", "{}",
                               plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS && state.queued_audio == 0);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated",
        "{\"breakAttributes\":{\"time\":\"1700000000300\\u0000junk\"}}",
        plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS && state.queued_audio == 0);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated",
        "{\"breakAttributes\":{\"time\":\"1700000000100\"}}", plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 100 && state.queued_audio == 0);

    /* No Event ID is required for audio, and stale START cannot revive playback.
     * Later per-Packet timestamps do not change the stream's classification. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS, 2) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_MIDDLE, EPOCH_MS + 900, 1) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_END, EPOCH_MS + 1000, 1) == TAI_OK);
    DEMO_CHECK(state.stream_start_ms == EPOCH_MS && state.queued_audio == 0);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_ONE_SHOT, EPOCH_MS + 200, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == 1);

    static const char *invalid[] = {
        "{}", "null", "[]", "{\"time\":null}", "{\"time\":true}",
        "{\"time\":{}}", "{\"time\":\"\"}", "{\"time\":\"0\"}",
        "{\"time\":0}", "{\"time\":-1}", "{\"time\":1.5}",
        "{\"time\":\"-1\"}", "{\"time\":\"+1700000000300\"}",
        "{\"time\":\"1700000000300x\"}", "{\"time\":\" 1700000000300\"}",
        "{\"time\":\"1700000000300.0\"}", "{\"time\":\"1e12\"}",
        "{\"time\":\"4398046511104\"}", "{\"time\":4398046511104}",
        "{\"time\":\"18446744073709551616000\"}",
        "{\"time\":\"1700000000300\\u0000junk\"}",
        "{\"time\":\"1700000000300\"", "{\"time\":\"1700000000300\"}garbage",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        on_ai_control("asrInterrupt", invalid[i], strlen(invalid[i]), &state);
        DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200 && state.queued_audio == 0);
    }
    on_ai_control("asrInterrupt", NULL, 0, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200 && state.queued_audio == 0);
    static const char bounded[] = "{\"time\":\"1700000000300\"}ignored";
    on_ai_control("asrInterrupt", bounded, sizeof(bounded) - 10, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200 && state.queued_audio == 0);
    on_ai_control("not-an-interrupt", bounded, sizeof(bounded) - 8, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200);

    /* Duplicates and older notices cannot flush fresh audio. A newer cutoff
     * still below the stream start is applied but must preserve it. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 250, 1) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200 && state.queued_audio == 1);
    demo_test_interrupt(&state, EPOCH_MS + 100);
    demo_test_interrupt(&state, EPOCH_MS + 50);
    static const char numeric[] = "{\"time\":1700000000150} \n";
    on_ai_control("asrInterrupt", numeric, sizeof(numeric) - 1, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 200 && state.queued_audio == 1);
    DEMO_CHECK(state.queued_start_ms[0] == EPOCH_MS + 250);
    static const char newer[] = "{\"time\":\"1700000000225\"}";
    on_ai_control("asrInterrupt", newer, sizeof(newer) - 1, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 225 && state.queued_audio == 1);
    DEMO_CHECK(state.queued_start_ms[0] == EPOCH_MS + 250);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated",
        "{\"breakAttributes\":{\"time\":\"1700000000175\"}}", plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 225 && state.queued_audio == 1);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_CHAT_BREAK, "unrelated",
        "{\"breakAttributes\":{\"time\":\"1700000000100\"}}", plausible_body) == TAI_OK);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 225 && state.queued_audio == 1);

    /* A bounded, non-NUL-terminated view is accepted, ignoring adjacent bytes. */
    on_ai_control("asrInterrupt", bounded, sizeof(bounded) - 8, &state);
    DEMO_CHECK(state.audio_cutoff_ms == EPOCH_MS + 300 && state.queued_audio == 0);

    /* Deterministically exercise both callback/admission race orderings. */
    tai_audio_msg_t audio = {
        .stream_flag = TAI_STREAM_START,
        .timestamp_ms = EPOCH_MS + 400,
        .data = (const uint8_t *)"opus",
        .len = 4,
    };
    DEMO_CHECK(on_flow_control(ctx, &state) == 1);
    demo_test_interrupt(&state, EPOCH_MS + 400);
    on_audio(ctx, &audio, &state);
    DEMO_CHECK(state.queued_audio == 0);
    audio.timestamp_ms = EPOCH_MS + 500;
    DEMO_CHECK(on_flow_control(ctx, &state) == 1);
    on_audio(ctx, &audio, &state);
    DEMO_CHECK(state.queued_audio == 1);
    demo_test_interrupt(&state, EPOCH_MS + 500);
    DEMO_CHECK(state.queued_audio == 0);

    /* Mixed queues retain each item's original stream start, not merely the
     * latest stream's timestamp. Preserve order while removing stale items. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 600, 1) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 900, 1) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 700, 1) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_ONE_SHOT, EPOCH_MS + 1000, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == PLAYBACK_CAPACITY);
    demo_test_interrupt(&state, EPOCH_MS + 700);
    DEMO_CHECK(state.queued_audio == 2);
    DEMO_CHECK(state.queued_start_ms[0] == EPOCH_MS + 900);
    DEMO_CHECK(state.queued_start_ms[1] == EPOCH_MS + 1000);
    demo_test_interrupt(&state, EPOCH_MS + 1000);
    DEMO_CHECK(state.queued_audio == 0);

    /* Ordinary pause/resume loses no codec frames; repeated START callbacks,
     * including resumed ones, all retain the same server timestamp. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 1100,
                               PLAYBACK_CAPACITY + 2) == TAI_OK);
    DEMO_CHECK(state.queued_audio == PLAYBACK_CAPACITY);
    DEMO_CHECK(ctx->rx_pending_len == 80);
    DEMO_CHECK(ctx->rx_pending_body == ctx->rx_buf + 8 + PLAYBACK_CAPACITY * 40);
    DEMO_CHECK(ctx->rx_pending_ts_ms == EPOCH_MS + 1100);
    for (size_t i = 0; i < state.queued_audio; i++)
        DEMO_CHECK(state.queued_start_ms[i] == EPOCH_MS + 1100);
    const uint8_t *cursor = ctx->rx_pending_body;
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx) == 1);
    DEMO_CHECK(ctx->rx_pending_body == cursor && ctx->rx_pending_len == 80);
    /* Consume one frame, resume exactly one, then consume the full queue. */
    state.queued_audio--;
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx) == 1);
    DEMO_CHECK(state.queued_audio == PLAYBACK_CAPACITY);
    DEMO_CHECK(ctx->rx_pending_body == cursor + 40 && ctx->rx_pending_len == 40);
    DEMO_CHECK(state.queued_start_ms[PLAYBACK_CAPACITY - 1] == EPOCH_MS + 1100);
    state.queued_audio = 0;
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx) == 0);
    DEMO_CHECK(ctx->rx_pending_body == NULL && ctx->rx_pending_len == 0);
    DEMO_CHECK(state.queued_audio == 1 && state.queued_start_ms[0] == EPOCH_MS + 1100);
    state.queued_audio = 0;

    /* Interrupt a paused START without touching any worker-owned pending state.
     * The worker then drains the retained frames through the real callback. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 1200,
                               PLAYBACK_CAPACITY + 2) == TAI_OK);
    cursor = ctx->rx_pending_body;
    demo_test_interrupt(&state, EPOCH_MS + 1200);
    DEMO_CHECK(state.queued_audio == 0 && on_flow_control(ctx, &state) == 1);
    DEMO_CHECK(ctx->rx_pending_body == cursor && ctx->rx_pending_len == 80);
    DEMO_CHECK(ctx->rx_pending_ts_ms == EPOCH_MS + 1200);
    DEMO_CHECK(ctx->rx_pending_flag == TAI_STREAM_START);
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx) == 0);
    DEMO_CHECK(ctx->rx_pending_body == NULL && ctx->rx_pending_len == 0);
    DEMO_CHECK(state.queued_audio == 0 && state.stream_start_ms == EPOCH_MS + 1200);

    /* Empty START/ONE_SHOT must latch through the real dispatcher but must not
     * enter playback. MIDDLE/END use that latch, not their own timestamps. */
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 1300, 0) == TAI_OK);
    DEMO_CHECK(state.stream_start_ms == EPOCH_MS + 1300 && state.queued_audio == 0);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_MIDDLE, EPOCH_MS, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == 1 && state.queued_start_ms[0] == EPOCH_MS + 1300);
    demo_test_interrupt(&state, EPOCH_MS + 1300);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_ONE_SHOT, EPOCH_MS + 1250, 0) == TAI_OK);
    DEMO_CHECK(state.stream_start_ms == EPOCH_MS + 1250 && state.queued_audio == 0);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_END, EPOCH_MS + 2000, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == 0);

    /* END for interrupted or previous Events cannot terminate the demo. */
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_START, "old", NULL, NULL) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 1200, 1) == TAI_OK);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_END, "old", NULL, NULL) == TAI_OK);
    DEMO_CHECK(state.done == 0);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_START, "new", NULL, NULL) == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, TAI_STREAM_START, EPOCH_MS + 1400, 1) == TAI_OK);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_END, "old", NULL, NULL) == TAI_OK);
    DEMO_CHECK(state.done == 0 && strcmp(state.current_event_id, "new") == 0);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_END, "new", NULL, NULL) == TAI_OK);
    DEMO_CHECK(state.done == 1);

    /* A later text-only Event must not inherit the interrupted audio latch. */
    demo_test_interrupt(&state, EPOCH_MS + 1400);
    state.done = 0;
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_START, "text-only", NULL, NULL) == TAI_OK);
    tai_text_msg_t text = { .text = "", .len = 0 };
    on_text(ctx, &text, &state);
    DEMO_CHECK(demo_test_event(ctx, TAI_EVT_END, "text-only", NULL, NULL) == TAI_OK);
    DEMO_CHECK(state.done == 1);

    /* The inclusive 42-bit upper bound is valid, without narrowing to 32 bits. */
    demo_test_interrupt(&state, MAX_AUDIO_TIME_MS);
    DEMO_CHECK(state.audio_cutoff_ms == MAX_AUDIO_TIME_MS && state.queued_audio == 0);

    /* No outer test mutex serializes these production callbacks: either order
     * must leave all <= cutoff audio discarded using only the playback mutex. */
    demo_state_t racing = {0};
    int mutex_rc = pthread_mutex_init(&racing.mutex, NULL);
    DEMO_CHECK(mutex_rc == 0);
    if (mutex_rc == 0) {
        for (int round = 0; round < 16; round++) {
            racing.audio_cutoff_ms = 0;
            racing.stream_start_ms = 0;
            racing.queued_audio = 0;
            pthread_t audio_thread, control_thread;
            int audio_rc = pthread_create(&audio_thread, NULL, demo_racing_audio, &racing);
            int control_rc = pthread_create(&control_thread, NULL, demo_racing_interrupt, &racing);
            DEMO_CHECK(audio_rc == 0 && control_rc == 0);
            if (audio_rc == 0) pthread_join(audio_thread, NULL);
            if (control_rc == 0) pthread_join(control_thread, NULL);
            DEMO_CHECK(racing.audio_cutoff_ms == EPOCH_MS + 1000);
            DEMO_CHECK(racing.queued_audio == 0);
        }
        DEMO_CHECK(pthread_mutex_destroy(&racing.mutex) == 0);
    }

cleanup:
    if (state.tai) tai_ctx_deinit(state.tai);
    pal->free(memory);
    DEMO_CHECK(pthread_mutex_destroy(&state.mutex) == 0);
    printf("mqtt_interrupt_demo_tests: %d failures\n", failures);
    return failures;
}

#undef DEMO_CHECK
