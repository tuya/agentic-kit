/*
 * tests/test_integration.c -- Deterministic integration tests.
 *
 * Uses tai_pal_loopback to drive the full send/receive pipeline without any
 * network. Each test covers what one example demonstrates:
 *
 *   test_text_query     - mirrors examples/text_query
 *   test_audio_roundtrip- mirrors examples/opus_chat (upload + TTS downlink)
 *   test_image_query    - mirrors examples/edu_camera (image + prompt)
 *   test_disconnect_eof - mirrors shutdown path (server EOF -> callback)
 *
 * Each test asserts on concrete protocol behaviour (packet sequence, attrs,
 * callback invocation) rather than just exit-code + string matches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "../src/tai_internal.h"
#include "tai_pal_loopback.h"

/* =========================================================================
 * Test harness
 * ========================================================================= */
static int g_pass = 0, g_fail = 0;

#define CHECK(expr)                                          \
    do {                                                     \
        if (!(expr)) {                                       \
            fprintf(stderr, "  FAIL  %s:%d  %s\n",           \
                    __FILE__, __LINE__, #expr);              \
            g_fail++;                                        \
        } else {                                             \
            g_pass++;                                        \
        }                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                 \
    do {                                                                   \
        long _va = (long)(a), _vb = (long)(b);                             \
        if (_va != _vb) {                                                  \
            fprintf(stderr, "  FAIL  %s:%d  %s == %s (got %ld vs %ld)\n", \
                    __FILE__, __LINE__, #a, #b, _va, _vb);                 \
            g_fail++;                                                      \
        } else {                                                           \
            g_pass++;                                                      \
        }                                                                  \
    } while (0)

#define SECTION(name) printf("\n[%s]\n", name)

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* =========================================================================
 * Shared test state (populated by callbacks).
 * ========================================================================= */
typedef struct {
    pthread_mutex_t mtx;

    int    text_calls;
    char   text_buf[4096];
    size_t text_len;
    uint8_t last_text_flag;

    int      audio_calls;
    size_t   audio_bytes;
    uint32_t audio_sample_rate;
    uint16_t audio_frame_duration;

    int      event_calls;
    uint16_t event_types[16];
    int      event_count;

    int      disconnect_calls;
    uint16_t disconnect_code;
} test_state_t;

static test_state_t g_st;

static void st_reset(void)
{
    pthread_mutex_lock(&g_st.mtx);
    g_st.text_calls       = 0;
    g_st.text_len         = 0;
    g_st.last_text_flag   = 0xFF;
    g_st.text_buf[0]      = '\0';
    g_st.audio_calls      = 0;
    g_st.audio_bytes      = 0;
    g_st.audio_sample_rate = 0;
    g_st.audio_frame_duration = 0;
    g_st.event_calls      = 0;
    g_st.event_count      = 0;
    g_st.disconnect_calls = 0;
    g_st.disconnect_code  = 0;
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_text(tai_ctx_t *ctx, const char *t, size_t l,
                    uint8_t flag, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.text_calls++;
    g_st.last_text_flag = flag;
    if (g_st.text_len + l < sizeof(g_st.text_buf)) {
        memcpy(g_st.text_buf + g_st.text_len, t, l);
        g_st.text_len += l;
        g_st.text_buf[g_st.text_len] = '\0';
    }
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_audio(tai_ctx_t *ctx, const uint8_t *d, size_t l,
                     uint32_t sample_rate, uint16_t frame_duration, void *ud)
{
    (void)ctx; (void)d; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.audio_calls++;
    g_st.audio_bytes += l;
    g_st.audio_sample_rate    = sample_rate;
    g_st.audio_frame_duration = frame_duration;
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_event(tai_ctx_t *ctx, uint16_t type,
                     const uint8_t *d, size_t l, void *ud)
{
    (void)ctx; (void)d; (void)l; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.event_calls++;
    if (g_st.event_count < (int)(sizeof(g_st.event_types)/sizeof(g_st.event_types[0])))
        g_st.event_types[g_st.event_count++] = type;
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_disconnect(tai_ctx_t *ctx, uint16_t code, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.disconnect_calls++;
    g_st.disconnect_code = code;
    pthread_mutex_unlock(&g_st.mtx);
}

/* Wait up to timeout_ms for condition on g_st. Returns 1 if met, 0 on timeout. */
#define WAIT_FOR(cond_expr, timeout_ms)                                 \
    ({                                                                  \
        int _ok = 0;                                                    \
        int _waited = 0;                                                \
        while (_waited < (timeout_ms)) {                                \
            pthread_mutex_lock(&g_st.mtx);                              \
            int _c = (cond_expr);                                       \
            pthread_mutex_unlock(&g_st.mtx);                            \
            if (_c) { _ok = 1; break; }                                 \
            sleep_ms(5);                                                \
            _waited += 5;                                               \
        }                                                               \
        _ok;                                                            \
    })

/* =========================================================================
 * Helper: decode client-sent frames captured by the loopback PAL.
 *
 * Drains all bytes from the TX FIFO and walks them frame-by-frame.
 * app_out is populated with pointers into tx_scratch (caller-owned).
 * ========================================================================= */
typedef struct {
    uint8_t  pkt_type;
    uint8_t  frag;
    tai_attr_t attrs[TAI_MAX_ATTRS];
    int      attr_count;
    const uint8_t *payload;
    size_t         payload_len;
    const uint8_t *app_bytes;
    size_t         app_len;
} captured_pkt_t;

/* Decode up to max_out frames from tx_scratch[0..tx_len].
 * initial_sig_len = 0 when called for the first handshake pop (ClientHello
 * is unsigned); after the ClientHello frame is seen, we auto-switch to 32
 * for the rest of this call. For later pops, pass 32 directly. */
static int decode_captured(uint8_t *tx, size_t tx_len,
                           uint8_t initial_sig_len,
                           captured_pkt_t *out, int max_out)
{
    int n = 0;
    size_t off = 0;
    uint8_t cur_sig_len = initial_sig_len;

    while (off < tx_len && n < max_out) {
        size_t remaining = tx_len - off;
        size_t frame_len = tai_frame_total_size(tx + off, remaining);
        if (frame_len == 0 || frame_len > remaining) break;

        uint16_t seq;
        const uint8_t *payload;
        size_t payload_len;

        int rc = tai_frame_decode(tx + off, frame_len, cur_sig_len,
                                   &out[n].frag, &seq, &payload, &payload_len);
        if (rc != TAI_OK) break;

        out[n].payload     = payload;
        out[n].payload_len = payload_len;

        /* Decode app packet from payload (for TAI_FRAG_NONE only; real
         * fragmentation would need reassembly, not needed for these tests). */
        if (out[n].frag == TAI_FRAG_NONE) {
            out[n].app_bytes = payload;
            out[n].app_len   = payload_len;
            rc = tai_packet_decode(TAI_VER_21, payload, payload_len,
                                    &out[n].pkt_type,
                                    out[n].attrs, TAI_MAX_ATTRS,
                                    &out[n].attr_count,
                                    &out[n].payload, &out[n].payload_len);
            if (rc != TAI_OK) {
                fprintf(stderr, "  decode_captured: packet_decode failed rc=%d\n", rc);
                break;
            }
        }

        /* After ClientHello (first frame), switch to sig_len=32. */
        if (n == 0 && out[n].pkt_type == TAI_PKT_CLIENT_HELLO) {
            cur_sig_len = 32;
        }

        n++;
        off += frame_len;
    }
    return n;
}

/* =========================================================================
 * Helper: build a server->client frame signed with ctx->sign_key.
 * ========================================================================= */
static size_t server_send(tai_ctx_t *ctx,
                          uint8_t pkt_type,
                          const tai_attr_t *attrs, int attr_count,
                          const uint8_t *payload, size_t payload_len,
                          uint16_t seq)
{
    uint8_t app[8192];
    int alen = tai_packet_encode(TAI_VER_21, pkt_type, attrs, attr_count,
                                  payload, payload_len, app, sizeof(app));
    if (alen <= 0) {
        fprintf(stderr, "  server_send: packet_encode rc=%d\n", alen);
        return 0;
    }

    uint8_t frame[8300];
    int flen = tai_frame_encode(TAI_FRAG_NONE, seq,
                                 app, (size_t)alen,
                                 ctx->sign_key, 32,
                                 ctx->pal, frame, sizeof(frame));
    if (flen <= 0) {
        fprintf(stderr, "  server_send: frame_encode rc=%d\n", flen);
        return 0;
    }
    tai_loopback_push_recv(frame, (size_t)flen);
    return (size_t)flen;
}

/* Build + push a server EVENT packet (e.g. End). */
static size_t server_send_event(tai_ctx_t *ctx, uint16_t evt_type, uint16_t seq)
{
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   ctx->event_id);

    uint8_t evtp[4];
    int elen = tai_pack_event(TAI_VER_21, evt_type, NULL, 0, evtp, sizeof(evtp));
    if (elen <= 0) return 0;

    return server_send(ctx, TAI_PKT_EVENT, attrs, 2, evtp, (size_t)elen, seq);
}

/* Build + push a server TEXT packet. */
static size_t server_send_text(tai_ctx_t *ctx, const char *text, size_t len,
                               uint8_t stream_flag, uint32_t seq_in_event,
                               uint16_t frame_seq)
{
    uint8_t payload[2048];
    int hlen = tai_pack_text_hdr(TAI_VER_21, TAI_DATA_ID_TEXT_DOWN,
                                  stream_flag, seq_in_event,
                                  payload, sizeof(payload));
    if (hlen <= 0) return 0;
    if ((size_t)hlen + len > sizeof(payload)) return 0;
    memcpy(payload + hlen, text, len);

    return server_send(ctx, TAI_PKT_TEXT, NULL, 0,
                       payload, (size_t)hlen + len, frame_seq);
}

/* Build + push a server AUDIO packet. */
static size_t server_send_audio(tai_ctx_t *ctx,
                                uint8_t stream_flag,
                                const char *audio_params_str,
                                const uint8_t *audio, size_t alen,
                                uint16_t frame_seq)
{
    uint8_t payload[4096];
    int hlen = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                   stream_flag, ctx->pal->time_ms(),
                                   payload, sizeof(payload));
    if (hlen <= 0) return 0;
    if ((size_t)hlen + alen > sizeof(payload)) return 0;
    if (alen) memcpy(payload + hlen, audio, alen);

    tai_attr_t attrs[1];
    int na = 0;
    if (audio_params_str) {
        attrs[na++] = tai_attr_strv(TAI_ATTR_AUDIO_PARAMS, audio_params_str);
    }

    return server_send(ctx, TAI_PKT_AUDIO, na ? attrs : NULL, na,
                       payload, (size_t)hlen + alen, frame_seq);
}

/* =========================================================================
 * Common setup/teardown
 * ========================================================================= */
static tai_ctx_t *setup_ctx(void *mem)
{
    tai_loopback_reset();
    tai_loopback_seed_random(42);
    st_reset();

    static tai_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.host             = "loopback.test";
    cfg.port             = 443;
    cfg.tls_sni          = "loopback.test";
    cfg.device_id        = "test-device-id";
    cfg.local_key        = "test-local-key-16";
    cfg.protocol_version = TAI_VER_21;
    cfg.client_type      = TAI_CLIENT_DEVICE;
    cfg.sign_level       = TAI_SIGN_HMAC_SHA256;
    cfg.biz_code         = 65537;
    cfg.biz_tag          = 119;
    cfg.disable_tls      = 1;  /* loopback PAL feeds raw TCP, no TLS */
    cfg.pal              = tai_pal_loopback();
    cfg.on_text          = on_text;
    cfg.on_audio         = on_audio;
    cfg.on_event         = on_event;
    cfg.on_disconnect    = on_disconnect;

    return tai_ctx_init(mem, &cfg);
}

/* =========================================================================
 * Test 1: text_query happy path
 * ========================================================================= */
static void test_text_query(void)
{
    SECTION("text_query");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);

    int rc = tai_connect(ctx);
    CHECK_EQ_INT(rc, TAI_OK);

    /* --- Verify handshake: ClientHello + SessionNew were sent. */
    uint8_t tx[8192];
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
    CHECK(txn > 0);

    captured_pkt_t pkts[8];
    int np = decode_captured(tx, txn, 0, pkts, 8);
    CHECK_EQ_INT(np, 2);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_CLIENT_HELLO);
    CHECK_EQ_INT(pkts[1].pkt_type, TAI_PKT_SESSION_NEW);

    /* ClientHello should carry client-type + security-suit. */
    const tai_attr_t *a;
    a = tai_attr_find(pkts[0].attrs, pkts[0].attr_count, TAI_ATTR_SECURITY_SUIT);
    CHECK(a && a->len == 33 && a->value[0] == TAI_SIGN_HMAC_SHA256);

    /* SessionNew should carry session-id + biz-code. */
    a = tai_attr_find(pkts[1].attrs, pkts[1].attr_count, TAI_ATTR_SESSION_ID);
    CHECK(a && a->len > 0);
    a = tai_attr_find(pkts[1].attrs, pkts[1].attr_count, TAI_ATTR_BIZ_CODE);
    CHECK(a && tai_attr_u32(a) == 65537);

    /* --- Send a text query and verify the 4-packet sequence. */
    const char *q = "hello world";
    rc = tai_send_text(ctx, q, strlen(q));
    CHECK_EQ_INT(rc, TAI_OK);

    /* Drain the 4 packets (EventStart, Text, EventPayloadsEnd, EventEnd). */
    sleep_ms(10);  /* small wait for sends to complete under lock */
    txn = tai_loopback_pop_sent(tx, sizeof(tx));
    np = decode_captured(tx, txn, 32, pkts, 8);
    CHECK_EQ_INT(np, 4);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_EVENT);
    CHECK_EQ_INT(pkts[1].pkt_type, TAI_PKT_TEXT);
    CHECK_EQ_INT(pkts[2].pkt_type, TAI_PKT_EVENT);
    CHECK_EQ_INT(pkts[3].pkt_type, TAI_PKT_EVENT);

    /* The TEXT packet's payload: [id:2][flags:1][varint_seq][text...] */
    CHECK(pkts[1].payload_len > 3);
    uint8_t tflags = pkts[1].payload[2];
    uint8_t stream_flag = (tflags >> 6) & 0x03;
    CHECK_EQ_INT(stream_flag, TAI_STREAM_ONE_SHOT);
    /* Text bytes at the end of payload must equal query */
    CHECK(pkts[1].payload_len >= 4 + strlen(q));
    const uint8_t *tbytes = pkts[1].payload + pkts[1].payload_len - strlen(q);
    CHECK(memcmp(tbytes, q, strlen(q)) == 0);

    /* --- Simulate server reply: Text(ONE_SHOT, "answer") then Event(END). */
    uint16_t srv_seq = 1;
    server_send_text(ctx, "answer!", 7, TAI_STREAM_ONE_SHOT, 1, srv_seq++);
    server_send_event(ctx, TAI_EVT_END, srv_seq++);

    /* --- Wait for callbacks. */
    int ok = WAIT_FOR(g_st.text_calls >= 1 && g_st.event_calls >= 1, 500);
    CHECK(ok);

    CHECK_EQ_INT(g_st.text_calls, 1);
    CHECK(strcmp(g_st.text_buf, "answer!") == 0);
    CHECK_EQ_INT(g_st.last_text_flag, TAI_STREAM_ONE_SHOT);
    CHECK(g_st.event_count >= 1);
    CHECK_EQ_INT(g_st.event_types[g_st.event_count - 1], TAI_EVT_END);

    /* --- Shutdown cleanly. */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);

    /* tai_disconnect should emit a SessionClose frame. */
    txn = tai_loopback_pop_sent(tx, sizeof(tx));
    np = decode_captured(tx, txn, 32, pkts, 8);
    CHECK(np >= 1);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_SESSION_CLOSE);
}

/* =========================================================================
 * Test 2: audio roundtrip (PCM upload + simulated Opus downlink)
 * ========================================================================= */
static void test_audio_roundtrip(void)
{
    SECTION("audio_roundtrip");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t tx[16384];
    size_t txn;
    captured_pkt_t pkts[16];
    int np;

    /* Discard handshake. */
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* --- Upload: audio_start + 2 chunks + audio_end. */
    CHECK_EQ_INT(tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000), TAI_OK);

    uint8_t chunk[320];
    memset(chunk, 0x42, sizeof(chunk));
    CHECK_EQ_INT(tai_send_audio_chunk(ctx, chunk, sizeof(chunk)), TAI_OK);
    CHECK_EQ_INT(tai_send_audio_chunk(ctx, chunk, sizeof(chunk)), TAI_OK);
    CHECK_EQ_INT(tai_send_audio_end(ctx), TAI_OK);

    sleep_ms(20);
    txn = tai_loopback_pop_sent(tx, sizeof(tx));
    np = decode_captured(tx, txn, 32, pkts, 16);

    /* Expected: EventStart + 2 Audio(START/MIDDLE) + Audio(END)
     *         + EventPayloadsEnd + EventEnd = 6 packets. */
    CHECK_EQ_INT(np, 6);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_EVENT);  /* EventStart */
    CHECK_EQ_INT(pkts[1].pkt_type, TAI_PKT_AUDIO);  /* chunk 1 (START) */
    CHECK_EQ_INT(pkts[2].pkt_type, TAI_PKT_AUDIO);  /* chunk 2 (MIDDLE) */
    CHECK_EQ_INT(pkts[3].pkt_type, TAI_PKT_AUDIO);  /* END */
    CHECK_EQ_INT(pkts[4].pkt_type, TAI_PKT_EVENT);  /* PayloadsEnd */
    CHECK_EQ_INT(pkts[5].pkt_type, TAI_PKT_EVENT);  /* EventEnd */

    /* First audio packet must carry audio-params attribute. */
    const tai_attr_t *ap = tai_attr_find(pkts[1].attrs, pkts[1].attr_count,
                                          TAI_ATTR_AUDIO_PARAMS);
    CHECK(ap && ap->len > 0);

    /* Media header stream flag check: START on first, MIDDLE on second, END on third. */
    CHECK(pkts[1].payload_len >= 3);
    CHECK_EQ_INT((pkts[1].payload[2] >> 6) & 0x03, TAI_STREAM_START);
    CHECK_EQ_INT((pkts[2].payload[2] >> 6) & 0x03, TAI_STREAM_MIDDLE);
    CHECK_EQ_INT((pkts[3].payload[2] >> 6) & 0x03, TAI_STREAM_END);

    /* --- Downlink: server sends TTS audio with audio_params. */
    uint8_t tts[160];
    memset(tts, 0x77, sizeof(tts));

    /* Opus-style 8-field audio_params: codec ch bits rate 0 bitrate dur size */
    server_send_audio(ctx, TAI_STREAM_START,
                      "111 1 16 16000 0 16000 20 40",
                      tts, 40, 10);
    /* Two more packets of 40 bytes each = one combined payload of 80 bytes,
     * the dispatcher should split on frame_size=40 -> 2 on_audio calls. */
    server_send_audio(ctx, TAI_STREAM_MIDDLE, NULL, tts, 80, 11);

    int ok = WAIT_FOR(g_st.audio_bytes >= 120, 500);
    CHECK(ok);
    CHECK(g_st.audio_calls >= 2);
    CHECK(g_st.audio_bytes >= 120);

    /* Verify rx audio params surfaced via the on_audio callback. */
    CHECK_EQ_INT(g_st.audio_sample_rate, 16000);
    CHECK_EQ_INT(g_st.audio_frame_duration,  20);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 3: image + text prompt (edu_camera flow)
 * ========================================================================= */
static void test_image_query(void)
{
    SECTION("image_query");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint8_t tx[32768];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* Fake JPEG bytes. Large enough (>1KB) to exercise mid-size packet path. */
    uint8_t img[4096];
    for (size_t i = 0; i < sizeof(img); i++) img[i] = (uint8_t)(i & 0xFF);

    int rc = tai_send_image_with_text(ctx,
                                       "what is this?", 13,
                                       img, sizeof(img),
                                       TAI_IMG_JPEG, 640, 480);
    CHECK_EQ_INT(rc, TAI_OK);

    sleep_ms(20);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
    captured_pkt_t pkts[16];
    int np = decode_captured(tx, txn, 32, pkts, 16);

    /* EventStart + Text + Image + EventPayloadsEnd + EventEnd = 5. */
    CHECK_EQ_INT(np, 5);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_EVENT);
    CHECK_EQ_INT(pkts[1].pkt_type, TAI_PKT_TEXT);
    CHECK_EQ_INT(pkts[2].pkt_type, TAI_PKT_IMAGE);
    CHECK_EQ_INT(pkts[3].pkt_type, TAI_PKT_EVENT);
    CHECK_EQ_INT(pkts[4].pkt_type, TAI_PKT_EVENT);

    /* Image packet has image-params and contains our bytes. */
    const tai_attr_t *ip = tai_attr_find(pkts[2].attrs, pkts[2].attr_count,
                                          TAI_ATTR_IMAGE_PARAMS);
    CHECK(ip && ip->len > 0);
    /* image-params format: "<payload_type> <fmt> <w> <h>" -> "0 1 640 480" */
    char buf[32];
    size_t clen = ip->len < sizeof(buf) - 1 ? ip->len : sizeof(buf) - 1;
    memcpy(buf, ip->value, clen);
    buf[clen] = '\0';
    CHECK(strcmp(buf, "0 1 640 480") == 0);

    /* Image payload: media_hdr(8) + img bytes */
    CHECK_EQ_INT(pkts[2].payload_len, 8 + sizeof(img));
    CHECK(memcmp(pkts[2].payload + 8, img, sizeof(img)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 4: server EOF triggers on_disconnect
 * ========================================================================= */
static void test_disconnect_eof(void)
{
    SECTION("disconnect_eof");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    /* Simulate server closing the TCP connection. */
    tai_loopback_close_connection();

    int ok = WAIT_FOR(g_st.disconnect_calls >= 1, 500);
    CHECK(ok);
    CHECK_EQ_INT(g_st.disconnect_calls, 1);

    /* tai_disconnect should still run cleanly after EOF. */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== Tuya AI -- integration tests (loopback PAL) ===\n");
    pthread_mutex_init(&g_st.mtx, NULL);

    test_text_query();
    test_audio_roundtrip();
    test_image_query();
    test_disconnect_eof();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    pthread_mutex_destroy(&g_st.mtx);
    return g_fail ? 1 : 0;
}
