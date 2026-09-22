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

static int demo_test_start(tai_ctx_t *ctx, const char *event_id)
{
    uint8_t payload[4];
    tai_attr_t attr = tai_attr_strv(TAI_ATTR_EVENT_ID, event_id);
    int len = tai_pack_event(TAI_VER_21, TAI_EVT_START, NULL, 0,
                             payload, sizeof(payload));
    if (len <= 0) return TAI_ERR_PROTO;
    return tai_proto_dispatch(ctx, TAI_PKT_EVENT, &attr, 1,
                              payload, (size_t)len);
}

static int demo_test_audio(tai_ctx_t *ctx, size_t frames)
{
    /* Keep the paused body's storage pinned in rx_buf, just like the worker. */
    int len = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                 TAI_STREAM_START, 123,
                                 ctx->rx_buf, sizeof(ctx->rx_buf));
    if (len <= 0) return TAI_ERR_PROTO;
    size_t body_len = frames * 40;
    if (body_len > sizeof(ctx->rx_buf) - (size_t)len) return TAI_ERR_MEM;
    memset(ctx->rx_buf + len, 0x5a, body_len);
    tai_attr_t attr = tai_attr_strv(TAI_ATTR_AUDIO_PARAMS,
                                    "111 1 16 16000 0 16000 20 40");
    return tai_proto_dispatch(ctx, TAI_PKT_AUDIO, &attr, 1,
                              ctx->rx_buf, (size_t)len + body_len);
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

    /* Mid-Packet pause: accepted frames fill playback, two remain retained.
     * Interruption must not mutate the worker-owned pending state. */
    DEMO_CHECK(demo_test_start(ctx, "paused-event") == TAI_OK);
    DEMO_CHECK(demo_test_audio(ctx, PLAYBACK_CAPACITY + 2) == TAI_OK);
    DEMO_CHECK(state.queued_audio == PLAYBACK_CAPACITY);
    DEMO_CHECK(ctx->rx_pending_valid == 1);
    DEMO_CHECK(ctx->rx_pending_offset == PLAYBACK_CAPACITY * 40);
    DEMO_CHECK(ctx->rx_pending_len == (PLAYBACK_CAPACITY + 2) * 40);
    DEMO_CHECK(strcmp(ctx->rx_pending_event_id, "paused-event") == 0);
    DEMO_CHECK(on_flow_control(ctx, &state) == 0);
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx, 0) == 1);
    DEMO_CHECK(ctx->rx_pending_offset == PLAYBACK_CAPACITY * 40);
    static const char paused_interrupt[] = "{\"eventId\":\"paused-event\"}";
    on_ai_control("asrInterrupt", paused_interrupt,
                  sizeof(paused_interrupt) - 1, &state);
    DEMO_CHECK(state.queued_audio == 0);
    DEMO_CHECK(ctx->rx_pending_valid == 1);
    DEMO_CHECK(ctx->rx_pending_discard == 0);
    DEMO_CHECK(ctx->rx_pending_offset == PLAYBACK_CAPACITY * 40);
    DEMO_CHECK(on_flow_control(ctx, &state) == 1);
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx, 0) == 0);
    DEMO_CHECK(ctx->rx_pending_valid == 0);
    DEMO_CHECK(ctx->rx_pending_offset == ctx->rx_pending_len);
    DEMO_CHECK(ctx->rx_pending_body == NULL);
    DEMO_CHECK(state.queued_audio == 0);

    /* Deterministically order the race: admission, interrupt, audio callback. */
    DEMO_CHECK(demo_test_start(ctx, "racing-event") == TAI_OK);
    tai_audio_msg_t audio = {
        .event_id = "racing-event",
        .stream_flag = TAI_STREAM_START,
        .data = (const uint8_t *)"opus",
        .len = 4,
    };
    DEMO_CHECK(on_flow_control(ctx, &state) == 1);
    static const char racing_interrupt[] = "{\"eventId\":\"racing-event\"}";
    on_ai_control("asrInterrupt", racing_interrupt,
                  sizeof(racing_interrupt) - 1, &state);
    on_audio(ctx, &audio, &state);
    DEMO_CHECK(state.queued_audio == 0);

    /* The opposite ordering accepts audio first, then flushes that queue. */
    DEMO_CHECK(demo_test_start(ctx, "queued-event") == TAI_OK);
    audio.event_id = "queued-event";
    DEMO_CHECK(on_flow_control(ctx, &state) == 1);
    on_audio(ctx, &audio, &state);
    DEMO_CHECK(state.queued_audio == 1);
    static const char queued_interrupt[] = "{\"eventId\":\"queued-event\"}";
    on_ai_control("asrInterrupt", queued_interrupt,
                  sizeof(queued_interrupt) - 1, &state);
    DEMO_CHECK(state.queued_audio == 0);
    on_audio(ctx, &audio, &state);
    DEMO_CHECK(state.queued_audio == 0);

    /* A distinct non-stale Event remains playable after scoped interruptions. */
    DEMO_CHECK(demo_test_start(ctx, "fresh-event") == TAI_OK);
    DEMO_CHECK(strcmp(state.current_event_id, "fresh-event") == 0);
    DEMO_CHECK(demo_test_audio(ctx, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == 1);
    DEMO_CHECK(ctx->rx_pending_valid == 0);

    /* Missing correlation also drains retained START frames without reopening
     * playback; only the next identified Event restores admission to playback. */
    DEMO_CHECK(demo_test_audio(ctx, PLAYBACK_CAPACITY + 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == PLAYBACK_CAPACITY);
    DEMO_CHECK(ctx->rx_pending_valid == 1);
    DEMO_CHECK(ctx->rx_pending_offset == (PLAYBACK_CAPACITY - 1) * 40);
    on_ai_control("asrInterrupt", "{}", 2, &state);
    DEMO_CHECK(state.queued_audio == 0);
    DEMO_CHECK(state.drop_unscoped_audio == 1);
    DEMO_CHECK(ctx->rx_pending_valid == 1);
    DEMO_CHECK(ctx->rx_pending_discard == 0);
    DEMO_CHECK(tai_proto_drain_pending_audio(ctx, 0) == 0);
    DEMO_CHECK(ctx->rx_pending_valid == 0);
    DEMO_CHECK(ctx->rx_pending_offset == ctx->rx_pending_len);
    DEMO_CHECK(state.queued_audio == 0);
    DEMO_CHECK(state.drop_unscoped_audio == 1);
    DEMO_CHECK(demo_test_start(ctx, "after-unscoped-event") == TAI_OK);
    DEMO_CHECK(state.drop_unscoped_audio == 0);
    DEMO_CHECK(demo_test_audio(ctx, 1) == TAI_OK);
    DEMO_CHECK(state.queued_audio == 1);

cleanup:
    if (state.tai) tai_ctx_deinit(state.tai);
    pal->free(memory);
    DEMO_CHECK(pthread_mutex_destroy(&state.mutex) == 0);
    printf("mqtt_interrupt_demo_tests: %d failures\n", failures);
    return failures;
}

#undef DEMO_CHECK
