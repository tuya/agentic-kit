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
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../src/tai_internal.h"
#include "tai_pal_loopback.h"
#include "test_log.h"

extern const pal_t *tai_pal_posix(void);

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
    size_t   audio_frame_lens[160]; /* per-call msg->len (carry-alignment check) */
    uint8_t  audio_concat[8192];    /* concatenation of all delivered frames     */
    size_t   audio_concat_len;

    int      image_calls;
    size_t   image_bytes;
    uint8_t  image_last_flag;
    uint8_t  image_format;
    uint16_t image_w, image_h;

    int      event_calls;
    uint16_t event_types[16];
    int      event_count;
    uint8_t  event_payload[4096];   /* last event's data (reassembly check) */
    size_t   event_payload_len;
    uint8_t  event_user_data[256];
    size_t   event_user_data_len;
    int      event_user_data_present;
    int      event_user_data_borrowed;

    int      disconnect_calls;
    uint16_t disconnect_code;
    uint8_t  disconnect_reason;
    uint8_t  disconnect_detail;
    int      text_calls_at_disconnect;
    uint64_t disconnect_ms;

    char     last_text_event_id[64];
    char     last_event_event_id[64];
} test_state_t;

static test_state_t g_st;
static pthread_t g_connect_thread;
static pthread_t g_text_callback_thread;

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
    g_st.audio_concat_len = 0;
    g_st.image_calls      = 0;
    g_st.image_bytes      = 0;
    g_st.image_last_flag  = 0xFF;
    g_st.image_format     = 0;
    g_st.image_w          = 0;
    g_st.image_h          = 0;
    g_st.event_calls      = 0;
    g_st.event_count      = 0;
    g_st.event_payload_len = 0;
    g_st.event_user_data_len = 0;
    g_st.event_user_data_present = 0;
    g_st.event_user_data_borrowed = 0;
    g_st.disconnect_calls = 0;
    g_st.disconnect_code  = 0;
    g_st.disconnect_reason = 0xFF;
    g_st.disconnect_detail = 0xFF;
    g_st.text_calls_at_disconnect = 0;
    g_st.disconnect_ms = 0;
    g_st.last_text_event_id[0]  = '\0';
    g_st.last_event_event_id[0] = '\0';
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_text_callback_thread = pthread_self();
    g_st.text_calls++;
    g_st.last_text_flag = msg->stream_flag;
    if (msg->event_id) {
        size_t n = strlen(msg->event_id);
        if (n >= sizeof(g_st.last_text_event_id)) n = sizeof(g_st.last_text_event_id) - 1;
        memcpy(g_st.last_text_event_id, msg->event_id, n);
        g_st.last_text_event_id[n] = '\0';
    }
    if (g_st.text_len + msg->len < sizeof(g_st.text_buf)) {
        memcpy(g_st.text_buf + g_st.text_len, msg->text, msg->len);
        g_st.text_len += msg->len;
        g_st.text_buf[g_st.text_len] = '\0';
    }
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    if (g_st.audio_calls < (int)(sizeof(g_st.audio_frame_lens)/sizeof(g_st.audio_frame_lens[0])))
        g_st.audio_frame_lens[g_st.audio_calls] = msg->len;
    g_st.audio_calls++;
    g_st.audio_bytes += msg->len;
    if (g_st.audio_concat_len + msg->len <= sizeof(g_st.audio_concat)) {
        memcpy(g_st.audio_concat + g_st.audio_concat_len, msg->data, msg->len);
        g_st.audio_concat_len += msg->len;
    }
    g_st.audio_sample_rate    = msg->sample_rate;
    g_st.audio_frame_duration = msg->frame_duration;
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_image(tai_ctx_t *ctx, const tai_image_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.image_calls++;
    g_st.image_bytes    += msg->len;
    g_st.image_last_flag = msg->stream_flag;
    if (msg->format) g_st.image_format = msg->format;
    if (msg->width)  g_st.image_w      = msg->width;
    if (msg->height) g_st.image_h      = msg->height;
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.event_calls++;
    if (g_st.event_count < (int)(sizeof(g_st.event_types)/sizeof(g_st.event_types[0])))
        g_st.event_types[g_st.event_count++] = msg->event_type;
    g_st.event_payload_len = msg->len;
    if (msg->data && msg->len <= sizeof(g_st.event_payload))
        memcpy(g_st.event_payload, msg->data, msg->len);
    g_st.event_user_data_len = msg->user_data_len;
    g_st.event_user_data_present = msg->user_data != NULL;
    if (msg->user_data && msg->user_data_len <= sizeof(g_st.event_user_data)) {
        memcpy(g_st.event_user_data, msg->user_data, msg->user_data_len);
        uintptr_t p = (uintptr_t)msg->user_data;
        g_st.event_user_data_borrowed =
            (p >= (uintptr_t)ctx->rx_buf &&
             p + msg->user_data_len <= (uintptr_t)ctx->rx_buf + sizeof(ctx->rx_buf)) ||
            (p >= (uintptr_t)ctx->frag_buf &&
             p + msg->user_data_len <= (uintptr_t)ctx->frag_buf + sizeof(ctx->frag_buf));
    }
    if (msg->event_id) {
        size_t n = strlen(msg->event_id);
        if (n >= sizeof(g_st.last_event_event_id)) n = sizeof(g_st.last_event_event_id) - 1;
        memcpy(g_st.last_event_event_id, msg->event_id, n);
        g_st.last_event_event_id[n] = '\0';
    }
    pthread_mutex_unlock(&g_st.mtx);
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    pthread_mutex_lock(&g_st.mtx);
    g_st.disconnect_calls++;
    g_st.disconnect_code   = msg->close_code;
    g_st.disconnect_reason = msg->reason;
    g_st.disconnect_detail = msg->detail;
    g_st.text_calls_at_disconnect = g_st.text_calls;
    g_st.disconnect_ms = ctx->pal->time_ms();
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
    tai_attr_t attrs[AGENTIC_KIT_TAI_MAX_ATTRS];
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
                                    out[n].attrs, AGENTIC_KIT_TAI_MAX_ATTRS,
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

/* Like server_send_event but with an explicit event_id (attr 61) value. */
static size_t server_send_event_id(tai_ctx_t *ctx, uint16_t evt_type,
                                   const char *eid, uint16_t seq)
{
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   eid);

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

/* Send one IMAGE chunk. image_params_str (e.g. "0 1 320 480" = raw/JPEG/w/h)
 * rides on START/ONE_SHOT only; pass NULL for MIDDLE/END. */
static size_t server_send_image(tai_ctx_t *ctx,
                                uint8_t stream_flag,
                                const char *image_params_str,
                                const uint8_t *img, size_t ilen,
                                uint16_t frame_seq)
{
    uint8_t payload[4096];
    int hlen = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_IMAGE_UP,
                                   stream_flag, ctx->pal->time_ms(),
                                   payload, sizeof(payload));
    if (hlen <= 0) return 0;
    if ((size_t)hlen + ilen > sizeof(payload)) return 0;
    if (ilen) memcpy(payload + hlen, img, ilen);

    tai_attr_t attrs[1];
    int na = 0;
    if (image_params_str)
        attrs[na++] = tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, image_params_str);

    return server_send(ctx, TAI_PKT_IMAGE, na ? attrs : NULL, na,
                       payload, (size_t)hlen + ilen, frame_seq);
}

/* =========================================================================
 * Helpers: transport fragmentation
 *
 * Build a full application packet, then frame its bytes as a sequence of
 * transport fragments (FRAG_FIRST/MIDDLE/LAST), each signed independently, and
 * push them to the recv FIFO. cuts[] are the END offsets of each fragment;
 * cuts[ncuts-1] must equal app_len. This exercises the receive path's
 * fragment reassembly (frag_buf) up to the whole-packet dispatch.
 * ========================================================================= */
static void server_send_app_fragmented(tai_ctx_t *ctx,
                                       const uint8_t *app, size_t app_len,
                                       const size_t *cuts, int ncuts,
                                       uint16_t *frame_seq)
{
    size_t off = 0;
    for (int i = 0; i < ncuts; i++) {
        size_t end = cuts[i];
        uint8_t frag = (i == 0)         ? TAI_FRAG_FIRST
                     : (i == ncuts - 1) ? TAI_FRAG_LAST
                                        : TAI_FRAG_MIDDLE;
        uint8_t frame[40000];
        int flen = tai_frame_encode(frag, (*frame_seq)++, app + off, end - off,
                                    ctx->sign_key, 32, ctx->pal,
                                    frame, sizeof(frame));
        if (flen <= 0) { fprintf(stderr, "  frag frame_encode rc=%d\n", flen); return; }
        tai_loopback_push_recv(frame, (size_t)flen);
        off = end;
    }
}

/* Build an AUDIO application packet (header byte + optional audio-params attr +
 * 8-byte media header + audio body) into app[]. Returns app length or <0. */
static int build_audio_app(tai_ctx_t *ctx, uint8_t stream_flag,
                           const char *params, const uint8_t *audio, size_t alen,
                           uint8_t *app, size_t app_cap)
{
    uint8_t payload[8192];
    int hlen = tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                                   stream_flag, ctx->pal->time_ms(),
                                   payload, sizeof(payload));
    if (hlen <= 0 || (size_t)hlen + alen > sizeof(payload)) return -1;
    if (alen) memcpy(payload + hlen, audio, alen);
    tai_attr_t attrs[1];
    int na = 0;
    if (params) attrs[na++] = tai_attr_strv(TAI_ATTR_AUDIO_PARAMS, params);
    return tai_packet_encode(TAI_VER_21, TAI_PKT_AUDIO, na ? attrs : NULL, na,
                             payload, (size_t)hlen + alen, app, app_cap);
}

/* Build a TEXT application packet ([data_id][flags][varint seq][text]). */
static int build_text_app(tai_ctx_t *ctx, uint8_t stream_flag, uint32_t seq,
                          const char *text, size_t tlen,
                          uint8_t *app, size_t app_cap)
{
    uint8_t payload[8192];
    int hlen = tai_pack_text_hdr(TAI_VER_21, TAI_DATA_ID_TEXT_DOWN,
                                  stream_flag, seq, payload, sizeof(payload));
    if (hlen <= 0 || (size_t)hlen + tlen > sizeof(payload)) return -1;
    if (tlen) memcpy(payload + hlen, text, tlen);
    return tai_packet_encode(TAI_VER_21, TAI_PKT_TEXT, NULL, 0,
                             payload, (size_t)hlen + tlen, app, app_cap);
}

/* Build an EVENT application packet (session-id + event-id attrs + packed
 * event payload) — used to exercise fragmented control-plane reassembly. */
static int build_event_app(tai_ctx_t *ctx, uint16_t evt_type,
                           const uint8_t *data, size_t dlen,
                           uint8_t *app, size_t app_cap)
{
    uint8_t evtp[4096];
    int elen = tai_pack_event(TAI_VER_21, evt_type, data, dlen, evtp, sizeof(evtp));
    if (elen <= 0) return -1;
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   ctx->event_id);
    return tai_packet_encode(TAI_VER_21, TAI_PKT_EVENT, attrs, 2,
                             evtp, (size_t)elen, app, app_cap);
}

/* =========================================================================
 * Common setup/teardown
 * ========================================================================= */
static tai_ctx_t *setup_ctx_config(void *mem, const tai_config_t *options)
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
    cfg.on_image         = on_image;
    cfg.on_event         = on_event;
    cfg.on_disconnect    = on_disconnect;
    if (options) {
        if (options->pal) cfg.pal = options->pal;
        cfg.on_flow_control = options->on_flow_control;
        cfg.ping_interval_ms = options->ping_interval_ms;
        cfg.ping_timeout_ms = options->ping_timeout_ms;
    }

    /* The loopback completes the handshake (signs the SessionNew ack with the
     * key derived from this local_key), so confirmed-connect succeeds. */
    tai_loopback_set_local_key(cfg.local_key);

    return tai_ctx_init(mem, &cfg);
}

static tai_ctx_t *setup_ctx(void *mem)
{
    return setup_ctx_config(mem, NULL);
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

    /* Fake JPEG bytes. Mid-size but within one transport fragment so the Image
     * packet stays FRAG_NONE (decode_captured only decodes whole frames). */
    uint8_t img[2048];
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

    /* Image packet detail (guarded: only if the Image decoded as a whole frame). */
    if (np >= 3 && pkts[2].pkt_type == TAI_PKT_IMAGE) {
        const tai_attr_t *ip = tai_attr_find(pkts[2].attrs, pkts[2].attr_count,
                                              TAI_ATTR_IMAGE_PARAMS);
        CHECK(ip && ip->len > 0);
        if (ip) {
            /* image-params format: "<payload_type> <fmt> <w> <h>" -> "0 1 640 480" */
            char buf[32];
            size_t clen = ip->len < sizeof(buf) - 1 ? ip->len : sizeof(buf) - 1;
            memcpy(buf, ip->value, clen);
            buf[clen] = '\0';
            CHECK(strcmp(buf, "0 1 640 480") == 0);
        }
        /* Image payload: media_hdr(8) + img bytes */
        CHECK_EQ_INT(pkts[2].payload_len, 8 + sizeof(img));
        CHECK(memcmp(pkts[2].payload + 8, img, sizeof(img)) == 0);
    }

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 3b: image + streamed audio in one event (photo + voice question)
 * ========================================================================= */
static void test_image_audio_query(void)
{
    SECTION("image_audio_query");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint8_t tx[32768];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* Fake JPEG bytes. Kept within one transport fragment (< max-fragment) so
     * the Image OneShot is a single frame and the packet sequence below is
     * deterministic; scatter-gather + fragmentation are covered elsewhere. */
    uint8_t img[2048];
    for (size_t i = 0; i < sizeof(img); i++) img[i] = (uint8_t)(i & 0xFF);

    uint8_t chunk[320];
    memset(chunk, 0x42, sizeof(chunk));

    CHECK_EQ_INT(tai_send_image_audio_start(ctx, img, sizeof(img), TAI_IMG_JPEG,
                                             640, 480,
                                             TAI_AUDIO_PCM, 1, 16, 16000),
                 TAI_OK);
    CHECK_EQ_INT(tai_send_image_audio_chunk(ctx, chunk, sizeof(chunk)), TAI_OK);
    CHECK_EQ_INT(tai_send_image_audio_end(ctx), TAI_OK);

    sleep_ms(20);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
    captured_pkt_t pkts[16];
    int np = decode_captured(tx, txn, 32, pkts, 16);

    /* EventStart + Image + Audio(START) + Audio(END)
     * + EventPayloadsEnd + EventEnd = 6. */
    CHECK_EQ_INT(np, 6);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_EVENT);  /* EventStart */
    CHECK_EQ_INT(pkts[1].pkt_type, TAI_PKT_IMAGE);  /* Image OneShot */
    CHECK_EQ_INT(pkts[2].pkt_type, TAI_PKT_AUDIO);  /* chunk (START) */
    CHECK_EQ_INT(pkts[3].pkt_type, TAI_PKT_AUDIO);  /* END */
    CHECK_EQ_INT(pkts[4].pkt_type, TAI_PKT_EVENT);  /* PayloadsEnd */
    CHECK_EQ_INT(pkts[5].pkt_type, TAI_PKT_EVENT);  /* EventEnd */

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
    /* EOF is a worker-detected transport fault (§3 detail differentiation). */
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_TRANSPORT);
    CHECK_EQ_INT(g_st.disconnect_detail, TAI_TRANSPORT_EOF);

    /* tai_disconnect should still run cleanly after EOF. */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: a server SESSION_CLOSE (connection_alive=1, link may persist) followed
 * by a real transport death must deliver BOTH on_disconnect callbacks. The
 * single-point guard latches only on TERMINAL disconnects, so the later
 * TRANSPORT death is NOT swallowed. (Regression: a fire-once guard that also
 * latched on the non-terminal SESSION_CLOSE silently dropped the connection-
 * death notification, leaving an idle app waiting forever on a dead link.)
 * ========================================================================= */
static void test_session_close_then_transport(void)
{
    SECTION("session_close_then_transport");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint8_t tx[2048];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* 1) Server SESSION_CLOSE: fires on_disconnect(SESSION_CLOSE); link stays up. */
    tai_attr_t a[1];
    a[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    server_send(ctx, TAI_PKT_SESSION_CLOSE, a, 1, NULL, 0, 1);
    CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_SESSION_CLOSE);

    /* 2) The link then actually dies (EOF). The worker must STILL fire a SECOND
     * on_disconnect(reason=TRANSPORT) — the guard must not swallow it. */
    tai_loopback_close_connection();
    CHECK(WAIT_FOR(g_st.disconnect_calls >= 2, 1000));
    CHECK_EQ_INT(g_st.disconnect_calls, 2);
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_TRANSPORT);
    CHECK_EQ_INT(g_st.disconnect_detail, TAI_TRANSPORT_EOF);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 5: a sustained downstream stream keeps the connection alive past the
 * liveness-timeout window even with no PONGs -- regression for the last_rx_ms
 * fix. Before the fix only PONGs advanced the liveness clock, so a long
 * no-pong stream tripped a spurious "pong timeout" disconnect.
 *
 * The loopback worker reads pal->time_ms() as real monotonic time (the
 * advance_time() virtual clock is not wired to it), so this is driven with a
 * short real timeout and real sleeps. It targets the last_rx_ms liveness
 * refresh; the drain time-budget (also real-time) is a production safeguard
 * not separately asserted here.
 * ========================================================================= */
static void test_liveness_during_stream(void)
{
    SECTION("liveness_during_stream");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);

    /* 1s of no liveness signal => dead; huge ping interval => no auto-pings,
     * isolating the last_rx_ms path (no PONG ever sent). */
    ctx->ping_timeout_ms  = 1000;
    ctx->ping_interval_ms = 1000000;

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t tx[4096];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));   /* discard handshake */

    /* Stream audio for ~1.35s (well past the 1s timeout), refreshing the rx
     * clock every 150ms. With the fix the link stays up; without it (PONG-only
     * liveness) it would trip a spurious timeout around round 7. */
    const uint8_t pcm[160] = {0};
    uint16_t srv_seq = 1;
    for (int round = 1; round <= 9; round++) {
        server_send_audio(ctx, TAI_STREAM_MIDDLE, NULL, pcm, sizeof(pcm), srv_seq++);
        CHECK(WAIT_FOR(g_st.audio_calls >= round, 1000));
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
        sleep_ms(150);
    }

    CHECK_EQ_INT(g_st.audio_calls, 9);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* on_text variant that ends the session from inside the callback via the
 * callback-safe request API. (tai_disconnect() here would join the worker
 * thread we're running on -> self-deadlock; tai_request_disconnect() must not.) */
static void on_text_then_request_disconnect(tai_ctx_t *ctx, const tai_text_msg_t *msg,
                                            void *ud)
{
    on_text(ctx, msg, ud);
    tai_request_disconnect(ctx);
}

/* =========================================================================
 * Test 6: tai_request_disconnect() is callback-safe. A receive callback (on
 * the worker thread) requests teardown; the worker stops on its own and the
 * owning thread's tai_disconnect() joins it WITHOUT the self-join deadlock that
 * calling tai_disconnect() from a callback would cause. A regression would hang
 * and trip the ctest timeout.
 * ========================================================================= */
static void test_request_disconnect_from_callback(void)
{
    SECTION("request_disconnect_from_callback");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    ctx->on_text = on_text_then_request_disconnect;   /* override default on_text */

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t tx[4096];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));   /* discard handshake */

    /* Server text -> on_text fires on the worker thread -> calls
     * tai_request_disconnect() (callback-safe, no join). */
    server_send_text(ctx, "bye", 3, TAI_STREAM_ONE_SHOT, 1, 1);
    CHECK(WAIT_FOR(g_st.text_calls >= 1, 1000));

    /* Owning thread tears down: joins the (already winding-down) worker without
     * hanging. Reaching the assert after proves there was no self-join. */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    CHECK_EQ_INT(g_st.text_calls, 1);
}

/* =========================================================================
 * Test 7: event_id latch (§4). A turn id from EVT_START persists across the
 * turn's packets that carry no attr 61 (inherited, not reset to ""), and is
 * cleared after EVT_END. Distinguishes §4 from §1's reset-if-absent behaviour.
 * ========================================================================= */
static void test_event_user_data(void)
{
    SECTION("event_user_data");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    if (!ctx) return;
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    const uint8_t user_data[] = "{\"serverTime\":123456789}";
    const uint8_t payload[] = { 'p', 0, 'x' };
    tai_attr_t attr = tai_attr_bytesv(TAI_ATTR_USER_DATA, user_data, sizeof(user_data) - 1);
    uint16_t seq = 100;
    for (int round = 0; round < 4; round++) {
        uint8_t evt[64], app[256];
        size_t len = round == 3 ? 0 : sizeof(payload);
        int elen = tai_pack_event(TAI_VER_21, TAI_EVT_CHAT_BREAK,
                                  payload, len, evt, sizeof(evt));
        CHECK(elen > 0);
        if (round == 1) {
            int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_EVENT, &attr, 1,
                                         evt, (size_t)elen, app, sizeof(app));
            CHECK(alen > 10);
            if (alen <= 10) break;
            size_t cuts[] = { 10, (size_t)alen };
            server_send_app_fragmented(ctx, app, (size_t)alen, cuts, 2, &seq);
        } else {
            CHECK(server_send(ctx, TAI_PKT_EVENT, round == 2 ? NULL : &attr,
                               round == 2 ? 0 : 1, evt, (size_t)elen, seq++) > 0);
        }
        CHECK(WAIT_FOR(g_st.event_calls == round + 1, 1000));
        pthread_mutex_lock(&g_st.mtx);
        CHECK_EQ_INT(g_st.event_payload_len, len);
        CHECK(memcmp(g_st.event_payload, payload, len) == 0);
        CHECK_EQ_INT(g_st.event_user_data_present, round != 2);
        CHECK_EQ_INT(g_st.event_user_data_len, round == 2 ? 0 : sizeof(user_data) - 1);
        if (round != 2) {
            CHECK(g_st.event_user_data_borrowed);
            CHECK(memcmp(g_st.event_user_data, user_data, sizeof(user_data) - 1) == 0);
        }
        pthread_mutex_unlock(&g_st.mtx);
    }
    tai_disconnect(ctx);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);
    tai_ctx_deinit(ctx);
}

static void test_event_id_latch(void)
{
    SECTION("event_id_latch");

    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t tx[2048];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));   /* discard handshake */

    uint16_t seq = 1;

    /* EVT_START carrying a turn id latches it. */
    server_send_event_id(ctx, TAI_EVT_START, "turn-42", seq++);
    CHECK(WAIT_FOR(g_st.event_calls >= 1, 1000));
    CHECK(strcmp(g_st.last_event_event_id, "turn-42") == 0);

    /* A TEXT with NO event_id attr inherits the latched id (§1 would give ""). */
    server_send_text(ctx, "hi", 2, TAI_STREAM_ONE_SHOT, 1, seq++);
    CHECK(WAIT_FOR(g_st.text_calls >= 1, 1000));
    CHECK(strcmp(g_st.last_text_event_id, "turn-42") == 0);

    /* EVT_END fires with the id, then the latch is cleared. */
    server_send_event_id(ctx, TAI_EVT_END, "turn-42", seq++);
    CHECK(WAIT_FOR(g_st.event_calls >= 2, 1000));
    CHECK(strcmp(g_st.last_event_event_id, "turn-42") == 0);

    /* A TEXT after END inherits the cleared (empty) id. */
    server_send_text(ctx, "bye", 3, TAI_STREAM_ONE_SHOT, 2, seq++);
    CHECK(WAIT_FOR(g_st.text_calls >= 2, 1000));
    CHECK(strcmp(g_st.last_text_event_id, "") == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 8: fail-fast (§3). Each malformed input must tear the connection down
 * with on_disconnect(reason=PROTOCOL, detail=<class>) — no self-heal, no
 * further callbacks. One fresh ctx per case (a fatal ends the connection).
 * ========================================================================= */
static void test_fail_fast(void)
{
    SECTION("fail_fast");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    uint8_t tx[2048];

    /* --- bad version: a desynced leading byte (version nibble != 2). --- */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));
        uint8_t bad[8] = { 0x00, 0, 0, 0, 0, 0, 0, 0 };   /* version 0 */
        tai_loopback_push_recv(bad, sizeof(bad));
        CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_PROTOCOL);
        CHECK_EQ_INT(g_st.disconnect_detail, TAI_PROTO_ERR_BAD_VERSION);
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }

    /* --- HMAC mismatch: a valid TEXT frame with one signature byte flipped. --- */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        uint8_t pl[64];
        int hl = tai_pack_text_hdr(TAI_VER_21, TAI_DATA_ID_TEXT_DOWN,
                                   TAI_STREAM_ONE_SHOT, 1, pl, sizeof(pl));
        CHECK(hl > 0);
        pl[hl] = 'x';
        uint8_t app[128];
        int al = tai_packet_encode(TAI_VER_21, TAI_PKT_TEXT, NULL, 0,
                                   pl, (size_t)hl + 1, app, sizeof(app));
        CHECK(al > 0);
        uint8_t fr[256];
        int fl = tai_frame_encode(TAI_FRAG_NONE, 1, app, (size_t)al,
                                  ctx->sign_key, 32, ctx->pal, fr, sizeof(fr));
        CHECK(fl > 0);
        fr[fl - 1] ^= 0xFF;   /* corrupt the last signature byte */
        tai_loopback_push_recv(fr, (size_t)fl);
        CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_PROTOCOL);
        CHECK_EQ_INT(g_st.disconnect_detail, TAI_PROTO_ERR_HMAC);
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }

    /* --- fragment orphan: a MIDDLE fragment with no preceding FIRST. --- */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));
        uint8_t body[16] = {0};
        uint8_t fr[128];
        int fl = tai_frame_encode(TAI_FRAG_MIDDLE, 1, body, sizeof(body),
                                  ctx->sign_key, 32, ctx->pal, fr, sizeof(fr));
        CHECK(fl > 0);
        tai_loopback_push_recv(fr, (size_t)fl);
        CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_PROTOCOL);
        CHECK_EQ_INT(g_st.disconnect_detail, TAI_PROTO_ERR_FRAG);
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }

    /* --- oversized frame: a header declaring a length larger than rx_buf can
     * never be assembled. The 5-byte header alone is enough for the worker to
     * detect it (needed > sizeof rx_buf) and fail-fast instead of stalling until
     * the liveness timeout. Leading byte 0x22 = v2.1 FRAG_NONE; len = 0xFFFF. */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));
        uint8_t hdr[5] = { 0x22, 0x00, 0x00, 0xFF, 0xFF };  /* len = 65535 */
        tai_loopback_push_recv(hdr, sizeof(hdr));
        CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_PROTOCOL);
        CHECK_EQ_INT(g_st.disconnect_detail, TAI_PROTO_ERR_OVERSIZED);
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }
}

/* =========================================================================
 * Test: forward-compatibility tolerance. An unknown packet type and an unknown
 * event type are well-formed (framing + HMAC valid) but unrecognised; the
 * client must SKIP them and keep the link up — a server that adds a new type
 * must not knock existing clients offline. We prove the stream stayed in sync
 * by delivering a valid TEXT right after the unknown packet and seeing it
 * arrive with no disconnect.
 * ========================================================================= */
/* =========================================================================
 * Test: received image (on_image, downlink). The server streams an image as
 * START (carrying image-params) + END; the client must deliver each chunk to
 * on_image with the parsed format/size and the raw bytes, with no disconnect.
 * ========================================================================= */
static void test_image_recv(void)
{
    SECTION("image_recv");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    uint8_t tx[2048];

    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* on_image delivers raw encoded bytes verbatim (no decode); a byte pattern
     * stands in for the JPEG body. */
    uint8_t body[64];
    for (int i = 0; i < (int)sizeof(body); i++) body[i] = (uint8_t)i;

    /* "0 1 320 480" = payload_type RAW, format JPEG, 320x480. */
    server_send_image(ctx, TAI_STREAM_START, "0 1 320 480", body, sizeof(body), 1);
    server_send_image(ctx, TAI_STREAM_END,   NULL,          NULL, 0,            2);

    CHECK(WAIT_FOR(g_st.image_calls >= 2, 1000));
    CHECK_EQ_INT((int)g_st.image_bytes, (int)sizeof(body));  /* START body + END(0) */
    CHECK_EQ_INT(g_st.image_format, TAI_IMG_JPEG);
    CHECK_EQ_INT(g_st.image_w, 320);
    CHECK_EQ_INT(g_st.image_h, 480);
    CHECK_EQ_INT(g_st.image_last_flag, TAI_STREAM_END);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);

    tai_disconnect(ctx); tai_ctx_deinit(ctx);
}

static void test_unknown_tolerated(void)
{
    SECTION("unknown_tolerated");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    uint8_t tx[2048];

    /* --- unknown packet type: a reserved type the device does not implement.
     * (IMAGE, once an example here, is now a handled type — see on_image.) --- */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        uint8_t pl[4] = {0};
        const uint8_t TAI_PKT_RESERVED_UNKNOWN = 99;   /* not enumerated / not dispatched */
        server_send(ctx, TAI_PKT_RESERVED_UNKNOWN, NULL, 0, pl, sizeof(pl), 1);
        /* A valid TEXT after the unknown packet: if the unknown frame was
         * skipped cleanly (stream in sync) this arrives; if it desynced or tore
         * down, text never comes. */
        server_send_text(ctx, "hi", 2, TAI_STREAM_ONE_SHOT, 1, 2);
        CHECK(WAIT_FOR(g_st.text_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_calls, 0);     /* no fail-fast teardown */
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }

    /* --- unknown EVENT type: a well-formed EVENT whose type is not enumerated. --- */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        server_send_event(ctx, 0xFFFE, 1);          /* unknown event type */
        server_send_text(ctx, "ok", 2, TAI_STREAM_ONE_SHOT, 1, 2);
        CHECK(WAIT_FOR(g_st.text_calls >= 1, 1000));
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
        CHECK_EQ_INT(g_st.event_calls, 0);          /* unknown event not emitted */
        tai_disconnect(ctx); tai_ctx_deinit(ctx);
    }
}

/* =========================================================================
 * Test: audio delivered over transport fragments. A single Opus audio packet
 * (CBR frame_size=40, 200-byte body = 5 frames) is split across 3 transport
 * fragments; the receive path reassembles the whole packet (frag_buf) and the
 * dispatcher splits the body into exactly 5 aligned 40-byte frames whose
 * concatenation equals the original — verifying reassembly + CBR Opus split.
 * ========================================================================= */
static void test_media_audio_fragmented(void)
{
    SECTION("media_audio_fragmented");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t body[200];
    for (int i = 0; i < 200; i++) body[i] = (uint8_t)(i & 0xFF);

    uint8_t app[512];
    int app_len = build_audio_app(ctx, TAI_STREAM_START,
                                  "111 1 16 16000 0 16000 20 40",
                                  body, sizeof(body), app, sizeof(app));
    CHECK(app_len > 0);

    /* Body split 30 / 100 / 70 — none a multiple of frame_size=40. */
    size_t body_start = (size_t)app_len - sizeof(body);
    size_t cuts[3] = { body_start + 30, body_start + 130, (size_t)app_len };
    uint16_t fseq = 100;
    server_send_app_fragmented(ctx, app, (size_t)app_len, cuts, 3, &fseq);

    CHECK(WAIT_FOR(g_st.audio_concat_len >= sizeof(body), 500));

    CHECK_EQ_INT(g_st.audio_calls, 5);
    CHECK_EQ_INT(g_st.audio_bytes, 200);
    for (int i = 0; i < 5 && i < g_st.audio_calls; i++)
        CHECK_EQ_INT(g_st.audio_frame_lens[i], 40);
    CHECK_EQ_INT(g_st.audio_concat_len, 200);
    CHECK(memcmp(g_st.audio_concat, body, sizeof(body)) == 0);

    /* rx audio params surfaced from the FIRST fragment's audio-params attr. */
    CHECK_EQ_INT(g_st.audio_sample_rate, 16000);
    CHECK_EQ_INT(g_st.audio_frame_duration, 20);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: trailing short frame after reassembly. A fragmented audio body whose
 * length is NOT a multiple of frame_size must split into N full frames plus a
 * final short frame:
 *   A) 210 bytes @ fs=40 -> 5 full + a 10-byte frame, and
 *   B) a body smaller than one frame (35 < 40), split FIRST+LAST, delivered as
 *      a single 35-byte frame (also the minimal 2-fragment shape).
 * ========================================================================= */
static void test_media_audio_remainder(void)
{
    SECTION("media_audio_remainder");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint16_t fseq = 100;

    /* A) 210-byte body, fs=40 -> 5 full frames + a 10-byte final frame. */
    {
        uint8_t body[210];
        for (int i = 0; i < 210; i++) body[i] = (uint8_t)(i & 0xFF);
        uint8_t app[512];
        int app_len = build_audio_app(ctx, TAI_STREAM_START,
                                      "111 1 16 16000 0 16000 20 40",
                                      body, sizeof(body), app, sizeof(app));
        CHECK(app_len > 0);
        size_t bs = (size_t)app_len - sizeof(body);
        size_t cuts[3] = { bs + 30, bs + 130, (size_t)app_len };
        server_send_app_fragmented(ctx, app, (size_t)app_len, cuts, 3, &fseq);

        CHECK(WAIT_FOR(g_st.audio_concat_len >= sizeof(body), 500));
        CHECK_EQ_INT(g_st.audio_calls, 6);
        CHECK_EQ_INT(g_st.audio_frame_lens[5], 10);   /* the flushed remainder */
        CHECK_EQ_INT(g_st.audio_concat_len, 210);
        CHECK(memcmp(g_st.audio_concat, body, sizeof(body)) == 0);
    }

    /* B) 35-byte body (< frame_size) split FIRST(30)+LAST(5): the straddle
     *    never completes and is flushed as one 35-byte final frame. */
    st_reset();
    {
        uint8_t body[35];
        for (int i = 0; i < 35; i++) body[i] = (uint8_t)(0xA0 + (i & 0x1F));
        uint8_t app[256];
        int app_len = build_audio_app(ctx, TAI_STREAM_START,
                                      "111 1 16 16000 0 16000 20 40",
                                      body, sizeof(body), app, sizeof(app));
        CHECK(app_len > 0);
        size_t bs = (size_t)app_len - sizeof(body);
        size_t cuts[2] = { bs + 30, (size_t)app_len };
        server_send_app_fragmented(ctx, app, (size_t)app_len, cuts, 2, &fseq);

        CHECK(WAIT_FOR(g_st.audio_concat_len >= sizeof(body), 500));
        CHECK_EQ_INT(g_st.audio_calls, 1);
        CHECK_EQ_INT(g_st.audio_frame_lens[0], 35);
        CHECK(memcmp(g_st.audio_concat, body, sizeof(body)) == 0);
    }

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: a whole (FRAG_NONE) audio packet is split into frame_size chunks plus a
 * trailing remainder. fs=1000 over a 3500-byte body -> 3 full frames + a 500
 * remainder. The packet stays within one transport frame (a downstream packet
 * cannot exceed AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD, which bounds rx_buf).
 * ========================================================================= */
static void test_media_audio_large_frame(void)
{
    SECTION("media_audio_large_frame");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint16_t fseq = 100;

    static uint8_t body[3500];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)((i * 7) & 0xFF);

    uint8_t app[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD];
    int app_len = build_audio_app(ctx, TAI_STREAM_START,
                                  "111 1 16 16000 0 16000 20 1000",  /* fs=1000 */
                                  body, sizeof(body), app, sizeof(app));
    CHECK(app_len > 0);

    uint8_t frame[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 64];
    int flen = tai_frame_encode(TAI_FRAG_NONE, fseq++, app, (size_t)app_len,
                                ctx->sign_key, 32, ctx->pal, frame, sizeof(frame));
    CHECK(flen > 0);
    tai_loopback_push_recv(frame, (size_t)flen);

    CHECK(WAIT_FOR(g_st.audio_concat_len >= sizeof(body), 500));
    /* 3500 / 1000 = 3 full frames + a 500 remainder. */
    CHECK_EQ_INT(g_st.audio_calls, 4);
    CHECK_EQ_INT(g_st.audio_frame_lens[0], 1000);
    CHECK_EQ_INT(g_st.audio_frame_lens[3], 500);
    CHECK_EQ_INT(g_st.audio_concat_len, 3500);
    CHECK(memcmp(g_st.audio_concat, body, sizeof(body)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: two consecutive downstream audio streams on one ctx (§2 / per-stream
 * params). Stream 1 negotiates Opus frame_size=40; stream 2 is PCM with no
 * frame_size. The parse-once cache must reset on the new stream's START so
 * stream 2 is delivered whole — not split on stream 1's stale frame_size.
 * ========================================================================= */
static void test_media_audio_two_streams(void)
{
    SECTION("media_audio_two_streams");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint16_t fseq = 100;

    /* Stream 1: Opus, frame_size=40, 80-byte body -> 2 frames. */
    uint8_t a1[80];
    memset(a1, 0x11, sizeof(a1));
    server_send_audio(ctx, TAI_STREAM_START, "111 1 16 16000 0 16000 20 40",
                      a1, sizeof(a1), fseq++);
    CHECK(WAIT_FOR(g_st.audio_calls >= 2, 500));
    CHECK_EQ_INT(g_st.audio_calls, 2);
    CHECK_EQ_INT(g_st.audio_sample_rate, 16000);

    /* Stream 2: PCM START, no frame_size -> must NOT inherit stream 1's fs=40. */
    st_reset();
    uint8_t a2[100];
    memset(a2, 0x22, sizeof(a2));
    server_send_audio(ctx, TAI_STREAM_START, "101 1 16 16000",
                      a2, sizeof(a2), fseq++);
    CHECK(WAIT_FOR(g_st.audio_calls >= 1, 500));
    CHECK_EQ_INT(g_st.audio_calls, 1);                /* whole body, not 40/40/20 */
    CHECK_EQ_INT(g_st.audio_frame_lens[0], 100);
    CHECK_EQ_INT(g_st.audio_concat_len, 100);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: fragmented control-plane (EVENT) reassembly (§2). A large EVENT packet
 * split across fragments must REASSEMBLE in frag_buf and fire on_event exactly
 * once with the complete payload — never stream incrementally like media.
 * ========================================================================= */
static void test_media_event_fragmented(void)
{
    SECTION("media_event_fragmented");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint16_t fseq = 100;

    static uint8_t json[2000];
    for (size_t i = 0; i < sizeof(json); i++) json[i] = (uint8_t)('A' + (i % 26));

    uint8_t app[4096];
    int app_len = build_event_app(ctx, TAI_EVT_UPDATE_CONTEXT, json, sizeof(json),
                                  app, sizeof(app));
    CHECK(app_len > 0);

    size_t cuts[3] = { 50, 1000, (size_t)app_len };
    server_send_app_fragmented(ctx, app, (size_t)app_len, cuts, 3, &fseq);

    CHECK(WAIT_FOR(g_st.event_calls >= 1, 500));
    CHECK_EQ_INT(g_st.event_calls, 1);                 /* exactly one dispatch */
    CHECK_EQ_INT(g_st.event_types[0], TAI_EVT_UPDATE_CONTEXT);
    CHECK_EQ_INT(g_st.event_payload_len, sizeof(json));
    CHECK(memcmp(g_st.event_payload, json, sizeof(json)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: reconnect after tearing down mid-stream (state reset). Receive
 * FRAG_FIRST + FRAG_MIDDLE of an audio packet (leaving a partial reassembly in
 * frag_buf, frag_state=1) with no FRAG_LAST, then tai_disconnect + tai_connect.
 * A fresh stream must deliver cleanly with no leftover bytes prepended.
 * ========================================================================= */
static void test_media_reconnect_midstream(void)
{
    SECTION("media_reconnect_midstream");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    uint16_t fseq = 100;

    /* FIRST(30 body) + MIDDLE(30 body) of an fs=40 stream; never send LAST. */
    uint8_t body[100];
    for (int i = 0; i < 100; i++) body[i] = (uint8_t)(i & 0xFF);
    uint8_t app[512];
    int app_len = build_audio_app(ctx, TAI_STREAM_START,
                                  "111 1 16 16000 0 16000 20 40",
                                  body, sizeof(body), app, sizeof(app));
    CHECK(app_len > 0);
    size_t bs = (size_t)app_len - sizeof(body);
    size_t ends[2] = { bs + 30, bs + 60 };
    size_t off = 0;
    for (int i = 0; i < 2; i++) {
        uint8_t flag = (i == 0) ? TAI_FRAG_FIRST : TAI_FRAG_MIDDLE;
        uint8_t fr[1024];
        int flen = tai_frame_encode(flag, fseq++, app + off, ends[i] - off,
                                    ctx->sign_key, 32, ctx->pal, fr, sizeof(fr));
        CHECK(flen > 0);
        tai_loopback_push_recv(fr, (size_t)flen);
        off = ends[i];
    }
    sleep_ms(30);                /* let the worker buffer the partial reassembly */

    /* Tear down mid-stream, then reconnect on the same ctx. */
    tai_disconnect(ctx);
    tai_loopback_reset();
    tai_loopback_set_local_key("test-local-key-16");
    st_reset();
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    /* Fresh stream: 80-byte fs=40 body -> exactly 2 clean 40-byte frames. */
    uint8_t body2[80];
    memset(body2, 0x55, sizeof(body2));
    server_send_audio(ctx, TAI_STREAM_START, "111 1 16 16000 0 16000 20 40",
                      body2, sizeof(body2), 1);
    CHECK(WAIT_FOR(g_st.audio_concat_len >= sizeof(body2), 500));
    CHECK_EQ_INT(g_st.audio_calls, 2);
    CHECK_EQ_INT(g_st.audio_frame_lens[0], 40);
    CHECK_EQ_INT(g_st.audio_frame_lens[1], 40);
    CHECK_EQ_INT(g_st.audio_concat_len, 80);
    CHECK(memcmp(g_st.audio_concat, body2, sizeof(body2)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: scatter-gather fragmented uplink (§6). An audio chunk larger than
 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD is streamed via send_app_sg, which fragments the
 * logical hdr||payload concat. Each frame is signed INDEPENDENTLY by the
 * segmented HMAC, so we verify every frame's HMAC, the FIRST/MIDDLE/LAST flags,
 * and that the reassembled application packet carries the exact pcm bytes
 * (proving zero-copy payload integrity across the fragment boundaries).
 * Sized to exactly 3 fragments regardless of the AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD knob.
 * ========================================================================= */
static void test_sg_audio_fragmented_uplink(void)
{
    SECTION("sg_audio_fragmented_uplink");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    static uint8_t tx[3 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));            /* discard handshake */

    CHECK_EQ_INT(tai_send_audio_start(ctx, TAI_AUDIO_OPUS, 1, 16, 16000), TAI_OK);
    sleep_ms(5);
    (void)tai_loopback_pop_sent(tx, sizeof(tx));            /* discard EventStart */

    /* 2 full fragments + a partial -> exactly FIRST/MIDDLE/LAST. */
    static uint8_t pcm[2 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 1000];
    for (size_t i = 0; i < sizeof(pcm); i++) pcm[i] = (uint8_t)((i * 5 + 3) & 0xFF);
    CHECK_EQ_INT(tai_send_audio_chunk(ctx, pcm, sizeof(pcm)), TAI_OK);
    sleep_ms(10);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
    CHECK(txn > 0);

    /* Walk frames: verify HMAC + flags, reassemble the application packet. */
    static uint8_t app[3 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    size_t app_len = 0, off = 0;
    int nframes = 0;
    uint8_t flags[8] = {0};
    while (off < txn && nframes < 8) {
        size_t flen = tai_frame_total_size(tx + off, txn - off);
        if (flen == 0 || flen > txn - off) break;
        CHECK(tai_frame_verify(tx + off, flen, 32, ctx->sign_key, ctx->pal) == TAI_OK);
        uint8_t frag; uint16_t seq; const uint8_t *pl; size_t pll;
        CHECK(tai_frame_decode(tx + off, flen, 32, &frag, &seq, &pl, &pll) == TAI_OK);
        flags[nframes] = frag;
        if (app_len + pll <= sizeof(app)) { memcpy(app + app_len, pl, pll); app_len += pll; }
        nframes++;
        off += flen;
    }
    CHECK_EQ_INT(nframes, 3);            /* (hdr + 2*MAX + 1000) over MAX = 3 */
    CHECK_EQ_INT(flags[0], TAI_FRAG_FIRST);
    CHECK_EQ_INT(flags[1], TAI_FRAG_MIDDLE);
    CHECK_EQ_INT(flags[2], TAI_FRAG_LAST);

    uint8_t pkt_type; tai_attr_t attrs[AGENTIC_KIT_TAI_MAX_ATTRS]; int na = 0;
    const uint8_t *payload; size_t payload_len;
    CHECK(tai_packet_decode(TAI_VER_21, app, app_len, &pkt_type, attrs,
                            AGENTIC_KIT_TAI_MAX_ATTRS, &na, &payload, &payload_len) == TAI_OK);
    CHECK_EQ_INT(pkt_type, TAI_PKT_AUDIO);
    CHECK_EQ_INT(payload_len, 8 + sizeof(pcm));     /* 8-byte media hdr + pcm */
    CHECK(memcmp(payload + 8, pcm, sizeof(pcm)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: MCP response over scatter-gather (§6.4). The JSON-RPC body is the
 * event payload, streamed zero-copy after the small [session][event][type]
 * header — the control buffer never holds the JSON. Verify the captured EVENT
 * frame carries event_type MCPCmd and the exact JSON as event data.
 * ========================================================================= */
static void test_sg_mcp_response(void)
{
    SECTION("sg_mcp_response");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t tx[8192];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));            /* discard handshake */

    const char *json = "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"ok\":true,\"tools\":[]}}";
    CHECK_EQ_INT(tai_send_mcp_response(ctx, json), TAI_OK);
    sleep_ms(5);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));

    captured_pkt_t pkts[4];
    int np = decode_captured(tx, txn, 32, pkts, 4);
    CHECK_EQ_INT(np, 1);
    CHECK_EQ_INT(pkts[0].pkt_type, TAI_PKT_EVENT);
    /* event payload = [event_type:2][json] */
    CHECK(pkts[0].payload_len == 2 + strlen(json));
    CHECK_EQ_INT(tai_r16(pkts[0].payload), TAI_EVT_MCP_CMD);
    CHECK(memcmp(pkts[0].payload + 2, json, strlen(json)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: scatter-gather send failures (§6.3 — no tx_broken latch).
 *  A) An app-thread mid-frame failure is reported synchronously as TAI_ERR_NET
 *     (bytes were committed -> stream desynced) but does NOT itself tear the
 *     connection down — that is the app's job on the error return. No
 *     on_disconnect fires, and the app's own tai_disconnect() is a clean stop.
 *  B) A failed CONTROL send behaves identically (synchronous error, no teardown).
 *  C) The worker's periodic Ping is its own TX health probe: when its send fails
 *     the worker (on its thread) tears the link down with
 *     on_disconnect(TRANSPORT, NET_ERROR) — this is how an idle broken uplink
 *     (invisible to the RX-side checks) is surfaced.
 * ========================================================================= */
static void test_sg_send_failure(void)
{
    SECTION("sg_send_failure");
    uint8_t tx[8192];

    /* A) app-thread SG mid-frame failure -> sync TAI_ERR_NET, no auto-disconnect. */
    {
        static uint8_t ctx_mem[sizeof(struct tai_ctx)];
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        ctx->ping_interval_ms = 1000000;   /* no auto-ping to interfere */
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));
        CHECK_EQ_INT(tai_send_audio_start(ctx, TAI_AUDIO_OPUS, 1, 16, 16000), TAI_OK);
        sleep_ms(5);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        /* Header write (call 0) ok, payload write (call 1) fails: mid-frame, bytes
         * already committed -> TAI_ERR_NET, but the SDK must NOT tear down.
         * pcm must push the whole frame over AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT so it
         * takes the 2-3-write scatter path — a coalesced small frame is a
         * single write and has no mid-frame boundary to fail on. */
        tai_loopback_fail_send_after(1);
        uint8_t pcm[AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT]; memset(pcm, 0x42, sizeof(pcm));
        CHECK_EQ_INT(tai_send_audio_chunk(ctx, pcm, sizeof(pcm)), TAI_ERR_NET);

        tai_loopback_fail_send_after(-1);
        sleep_ms(30);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);   /* no SDK-initiated teardown */

        tai_disconnect(ctx);                       /* app-driven clean stop */
        CHECK_EQ_INT(g_st.disconnect_calls, 0);    /* clean stop fires nothing */
        tai_ctx_deinit(ctx);
    }

    /* B) app-thread CONTROL send failure -> sync error, no auto-disconnect. */
    {
        static uint8_t ctx_mem[sizeof(struct tai_ctx)];
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        ctx->ping_interval_ms = 1000000;
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        tai_loopback_fail_send_after(0);   /* the control frame's first write fails */
        CHECK(tai_chat_break(ctx) != TAI_OK);
        tai_loopback_fail_send_after(-1);
        sleep_ms(30);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);

        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }

    /* C) worker Ping send failure -> TRANSPORT disconnect (idle-break detection). */
    {
        static uint8_t ctx_mem[sizeof(struct tai_ctx)];
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        ctx->ping_interval_ms = 50;        /* ping soon (loopback uses real time) */
        ctx->ping_timeout_ms  = 100000;    /* keep liveness from tripping first */
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        (void)tai_loopback_pop_sent(tx, sizeof(tx));

        tai_loopback_fail_send_after(0);   /* the next write (the periodic ping) fails */
        CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 2000));
        CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_TRANSPORT);
        CHECK_EQ_INT(g_st.disconnect_detail, TAI_TRANSPORT_NET_ERROR);
        tai_loopback_fail_send_after(-1);

        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }
}

/* Walk captured client frames, verify each HMAC (sig_len=32), and reassemble
 * the single transport-fragmented packet (FRAG_FIRST..LAST) into out. FRAG_NONE
 * control frames are skipped. Returns the fragment count, or <0 (no fragmented
 * packet / HMAC failure / overflow). */
static int sg_reassemble_fragged(tai_ctx_t *ctx, const uint8_t *tx, size_t txn,
                                 uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t off = 0, app_len = 0;
    int nf = 0, in_frag = 0, done = 0;
    while (off < txn) {
        size_t flen = tai_frame_total_size(tx + off, txn - off);
        if (flen == 0 || flen > txn - off) break;
        if (tai_frame_verify(tx + off, flen, 32, ctx->sign_key, ctx->pal) != TAI_OK) return -1;
        uint8_t frag; uint16_t seq; const uint8_t *pl; size_t pll;
        if (tai_frame_decode(tx + off, flen, 32, &frag, &seq, &pl, &pll) != TAI_OK) return -2;
        if (frag == TAI_FRAG_FIRST) { in_frag = 1; app_len = 0; nf = 0; }
        if (in_frag) {
            if (app_len + pll > out_cap) return -3;
            memcpy(out + app_len, pl, pll); app_len += pll; nf++;
            if (frag == TAI_FRAG_LAST) { done = 1; break; }
        }
        off += flen;
    }
    if (!done) return -4;
    *out_len = app_len;
    return nf;
}

/* =========================================================================
 * Test: scatter-gather fragmented IMAGE uplink (§6, finding #2). The image
 * header (image-params attr + 8-byte media hdr) is a different length than the
 * audio header, so this pins the hdr_len-dependent fragment mapping for a
 * non-audio packet: verify each frame HMAC, the FIRST/MIDDLE/LAST flags, and
 * that the reassembled IMAGE packet carries the exact image bytes.
 * ========================================================================= */
static void test_sg_image_fragmented_uplink(void)
{
    SECTION("sg_image_fragmented_uplink");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    static uint8_t tx[3 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    static uint8_t img[2 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 1000];   /* -> 3 fragments */
    for (size_t i = 0; i < sizeof(img); i++) img[i] = (uint8_t)((i * 11 + 7) & 0xFF);
    CHECK_EQ_INT(tai_send_image(ctx, img, sizeof(img), TAI_IMG_JPEG, 640, 480), TAI_OK);
    sleep_ms(10);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
    CHECK(txn > 0);

    static uint8_t app[3 * AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    size_t app_len = 0;
    int nf = sg_reassemble_fragged(ctx, tx, txn, app, sizeof(app), &app_len);
    CHECK_EQ_INT(nf, 3);                          /* (img + hdr) over MAX = 3 */

    uint8_t pt; tai_attr_t attrs[AGENTIC_KIT_TAI_MAX_ATTRS]; int na = 0;
    const uint8_t *payload; size_t payload_len;
    CHECK(tai_packet_decode(TAI_VER_21, app, app_len, &pt, attrs, AGENTIC_KIT_TAI_MAX_ATTRS,
                            &na, &payload, &payload_len) == TAI_OK);
    CHECK_EQ_INT(pt, TAI_PKT_IMAGE);
    CHECK(tai_attr_find(attrs, na, TAI_ATTR_IMAGE_PARAMS) != NULL);
    CHECK_EQ_INT(payload_len, 8 + sizeof(img));   /* media hdr + image */
    CHECK(memcmp(payload + 8, img, sizeof(img)) == 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: scatter-gather fragmented TEXT uplink + fragment boundary (§6,
 * findings #2/#5). The text header is 5 bytes (pkt byte + [id:2][flags:1]
 * [varint seq=0:1]). A body sized so hdr+body == MAX+1 must split into EXACTLY
 * two frames (FIRST + a 1-byte LAST); a body sized so hdr+body == MAX must NOT
 * fragment at all. Pins the chunk/flag boundary math at AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD.
 * ========================================================================= */
static void test_sg_text_fragmented_uplink(void)
{
    SECTION("sg_text_fragmented_uplink");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    static uint8_t tx[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));

    /* body = MAX-4 -> total (5 + MAX-4) = MAX+1 = one past the limit -> 2 frames. */
    static char txt[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD - 4];
    for (size_t i = 0; i < sizeof(txt); i++) txt[i] = (char)('a' + (i % 26));
    CHECK_EQ_INT(tai_send_text(ctx, txt, sizeof(txt)), TAI_OK);
    sleep_ms(10);
    size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));

    static uint8_t app[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD + 512];
    size_t app_len = 0;
    int nf = sg_reassemble_fragged(ctx, tx, txn, app, sizeof(app), &app_len);
    CHECK_EQ_INT(nf, 2);                           /* FIRST + 1-byte LAST */

    uint8_t pt; tai_attr_t attrs[AGENTIC_KIT_TAI_MAX_ATTRS]; int na = 0;
    const uint8_t *payload; size_t payload_len;
    CHECK(tai_packet_decode(TAI_VER_21, app, app_len, &pt, attrs, AGENTIC_KIT_TAI_MAX_ATTRS,
                            &na, &payload, &payload_len) == TAI_OK);
    CHECK_EQ_INT(pt, TAI_PKT_TEXT);
    CHECK(payload_len >= sizeof(txt));
    CHECK(memcmp(payload + payload_len - sizeof(txt), txt, sizeof(txt)) == 0);

    /* Boundary: body = MAX-5 -> total == MAX -> single frame, no fragmentation
     * (sg_reassemble_fragged finds no FRAG_FIRST). */
    (void)tai_loopback_pop_sent(tx, sizeof(tx));
    static char txt2[AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD - 5];
    for (size_t i = 0; i < sizeof(txt2); i++) txt2[i] = (char)('A' + (i % 26));
    CHECK_EQ_INT(tai_send_text(ctx, txt2, sizeof(txt2)), TAI_OK);
    sleep_ms(10);
    txn = tai_loopback_pop_sent(tx, sizeof(tx));
    CHECK(sg_reassemble_fragged(ctx, tx, txn, app, sizeof(app), &app_len) < 0);

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: small-frame coalesce boundary (AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT). One text
 * send emits 4 packets: EventStart / PayloadsEnd / EventEnd are CONTROL frames
 * assembled in tx_ctrl_buf — so on the coalesced path the payload and the
 * scratch buffer are the SAME memory (the aliasing hazard) — while the Text
 * frame's payload lives in the caller's buffer. A body sized so the whole Text
 * frame (5 frame hdr + 5 text hdr + body + 32 sig) is one byte UNDER the limit
 * coalesces; one byte larger crosses to the zero-copy scatter path. Every
 * frame must land HMAC-intact on both sides, with byte-exact Text payloads.
 * ========================================================================= */
static void test_sg_coalesce_boundary(void)
{
    SECTION("sg_coalesce_boundary");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    static uint8_t tx[4096];
    (void)tai_loopback_pop_sent(tx, sizeof(tx));             /* discard handshake */

    /* text hdr = pkt byte + [id:2][flags:1][varint seq:1] = 5 bytes. */
    static char under[AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT - 5 - 5 - 32 - 1];
    static char over [AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT - 5 - 5 - 32];
    for (size_t i = 0; i < sizeof(under); i++) under[i] = (char)('a' + (i % 26));
    for (size_t i = 0; i < sizeof(over);  i++) over[i]  = (char)('A' + (i % 26));

    for (int round = 0; round < 2; round++) {
        const char *body = round ? over : under;
        size_t body_len = round ? sizeof(over) : sizeof(under);
        CHECK_EQ_INT(tai_send_text(ctx, body, body_len), TAI_OK);
        sleep_ms(10);
        size_t txn = tai_loopback_pop_sent(tx, sizeof(tx));
        CHECK(txn > 0);

        /* Walk every frame of the event: all HMAC-verified (the aliased
         * coalesce corruption would fail verify/decode), exactly one TEXT. */
        size_t off = 0;
        int ntext = 0, nframes = 0;
        while (off < txn) {
            size_t flen = tai_frame_total_size(tx + off, txn - off);
            if (flen == 0 || flen > txn - off) break;
            CHECK(tai_frame_verify(tx + off, flen, 32, ctx->sign_key, ctx->pal) == TAI_OK);
            uint8_t frag, pt; uint16_t seq; const uint8_t *pl; size_t pll;
            CHECK(tai_frame_decode(tx + off, flen, 32, &frag, &seq, &pl, &pll) == TAI_OK);
            CHECK_EQ_INT(frag, TAI_FRAG_NONE);              /* never fragments here */
            tai_attr_t attrs[AGENTIC_KIT_TAI_MAX_ATTRS]; int na = 0;
            const uint8_t *payload; size_t payload_len;
            CHECK(tai_packet_decode(TAI_VER_21, pl, pll, &pt, attrs, AGENTIC_KIT_TAI_MAX_ATTRS,
                                    &na, &payload, &payload_len) == TAI_OK);
            if (pt == TAI_PKT_TEXT) {
                ntext++;
                /* decoded payload drops the pkt byte: [id:2][flags:1][varint] + body */
                CHECK_EQ_INT(payload_len, 4 + body_len);
                CHECK(memcmp(payload + 4, body, body_len) == 0);
            }
            nframes++;
            off += flen;
        }
        CHECK_EQ_INT(nframes, 4);                           /* Start/Text/PEnd/End */
        CHECK_EQ_INT(ntext, 1);
    }

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test: control-packet attribute JSON vs the tx_ctrl_buf limit (§6.5, finding
 * #4). The session/event JSON is escaped into the attribute block on the
 * contiguous control path. An oversized event JSON (escaped > 8 KB) must return
 * TAI_ERR_MEM from the build — not overflow a buffer or truncate silently.
 * ========================================================================= */
static void test_sg_control_json_limit(void)
{
    SECTION("sg_control_json_limit");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];

    tai_loopback_reset();
    tai_loopback_seed_random(42);
    st_reset();

    /* 5000 quote chars -> each escapes to 2 bytes -> ~10 KB escaped > 8 KB. */
    static char big_json[5000];
    memset(big_json, '"', sizeof(big_json) - 1);
    big_json[sizeof(big_json) - 1] = '\0';

    static tai_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.host = "loopback.test"; cfg.port = 443; cfg.tls_sni = "loopback.test";
    cfg.device_id = "test-device-id"; cfg.local_key = "test-local-key-16";
    cfg.protocol_version = TAI_VER_21; cfg.client_type = TAI_CLIENT_DEVICE;
    cfg.sign_level = TAI_SIGN_HMAC_SHA256; cfg.biz_code = 65537; cfg.biz_tag = 119;
    cfg.disable_tls = 1; cfg.pal = tai_pal_loopback();
    cfg.on_text = on_text; cfg.on_audio = on_audio;
    cfg.on_event = on_event; cfg.on_disconnect = on_disconnect;
    cfg.event_user_data_json = big_json;   /* lands in EventStart's attr block */
    tai_loopback_set_local_key(cfg.local_key);

    tai_ctx_t *ctx = tai_ctx_init(ctx_mem, &cfg);
    CHECK(ctx != NULL);
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    /* EventStart would carry the escaped JSON > tx_ctrl_buf -> TAI_ERR_MEM,
     * surfaced through tai_send_audio_start. No crash, no truncation. */
    int rc = tai_send_audio_start(ctx, TAI_AUDIO_OPUS, 1, 16, 16000);
    CHECK(rc < 0);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);   /* build failed pre-wire; stream intact, no teardown */

    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

/* =========================================================================
 * Test 9: confirmed connect (§3.5). tai_connect completes on either the
 * server's SessionNew ack (status OK) or an AuthenticateResponse (pkt 3,
 * connection-status-code 200, the production path); a missing ack times out
 * and a non-OK status is rejected. (The SessionNew OK path is exercised by
 * every other test via the loopback handshake; here we pin the rest.)
 * ========================================================================= */
static void test_confirmed_connect(void)
{
    SECTION("confirmed_connect");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];

    /* OK: the loopback acks -> connect succeeds. */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }

    /* Coalesced ack + application data: tai_connect consumes only the ack and
     * leaves Text buffered for the receive worker. The callback must not run on
     * the connecting thread even when both Frames arrived in one recv. */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        g_connect_thread = pthread_self();
        memset(&g_text_callback_thread, 0, sizeof(g_text_callback_thread));
        tai_loopback_set_handshake_mode(TAI_LB_HS_ACK_WITH_TEXT);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        CHECK(WAIT_FOR(g_st.text_calls == 1, 1000));
        CHECK(strcmp(g_st.text_buf, "coalesced") == 0);
        CHECK(!pthread_equal(g_connect_thread, g_text_callback_thread));
        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }

    /* AuthenticateResponse ack: the production server confirms the connect with
     * an AuthenticateResponse (pkt 3) carrying connection-status-code 200, NOT a
     * SessionNew ack. tai_connect must complete. (Regression: pkt 3 used to fall
     * through to the unknown/forward-compat path, so the wait timed out.) */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        tai_loopback_set_handshake_mode(TAI_LB_HS_AUTH_OK);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
    }

    /* Timeout: server never acks -> connect fails within connect_timeout_ms.
     * (Under the old optimistic connect this would have returned TAI_OK.) */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        ctx->connect_timeout_ms = 200;
        tai_loopback_set_handshake_mode(TAI_LB_HS_NO_ACK);
        CHECK(tai_connect(ctx) != TAI_OK);
        tai_ctx_deinit(ctx);
    }

    /* Reject: server acks with a non-OK status -> connect fails. */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        tai_loopback_set_handshake_mode(TAI_LB_HS_REJECT);
        CHECK(tai_connect(ctx) != TAI_OK);
        tai_ctx_deinit(ctx);
    }

    /* SESSION_CLOSE mid-handshake: the server closes the session before the
     * SessionNew ack. tai_connect must FAIL, and on_disconnect must NOT fire on
     * the connecting thread — a failed handshake is signaled by the return
     * value, and callbacks are a worker-thread contract. (Regression for the
     * confirmed-connect callback-thread fix: without the `connecting` guard the
     * SESSION_CLOSE dispatched on the connect thread fired on_disconnect.) */
    {
        tai_ctx_t *ctx = setup_ctx(ctx_mem);
        CHECK(ctx != NULL);
        ctx->connect_timeout_ms = 200;
        tai_loopback_set_handshake_mode(TAI_LB_HS_SESSION_CLOSE);
        CHECK(tai_connect(ctx) != TAI_OK);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);   /* no callback on the connect thread */
        tai_ctx_deinit(ctx);
    }
}

/* =========================================================================
 * Test 10: tai_disconnect latency. When the worker is idle (blocked waiting for
 * the next ping), tai_disconnect must still return promptly -- it must NOT wait
 * out a whole ping interval for the worker's blocking recv to expire. We raise
 * the loopback recv cap so it honours the full requested timeout like a real
 * PAL, set a long ping interval, then assert disconnect finishes well within it
 * (bounded by the SDK worker poll cap, AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS). A clean
 * shutdown must also fire NO on_disconnect callback.
 * ========================================================================= */
static void test_disconnect_latency(void)
{
    SECTION("disconnect_latency");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];

    tai_ctx_t *ctx = setup_ctx(ctx_mem);
    CHECK(ctx != NULL);
    /* Ping interval set well above the worker poll cap: an UN-capped worker would
     * block ~ping_interval in recv, while a capped one wakes within the cap.
     * Derive both from AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS so this stays correct if the cap is
     * retuned. */
    const uint32_t cap = AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS;
    ctx->ping_interval_ms = cap * 5;
    /* Make the loopback honour the full recv timeout, like a production PAL, so
     * the SDK worker poll cap (not the loopback's 50 ms default) bounds it. */
    tai_loopback_set_recv_cap_ms(cap * 5 + 5000);

    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    /* Let the worker settle into its (capped) blocking recv on an idle link. */
    sleep_ms(150);

    uint64_t t0 = ctx->pal->time_ms();
    tai_disconnect(ctx);
    uint64_t elapsed = ctx->pal->time_ms() - t0;
    printf("  tai_disconnect returned in %llu ms (cap=%u)\n",
           (unsigned long long)elapsed, (unsigned)cap);

    /* Capped: ~cap + teardown overhead. Without the cap the join would block out
     * the whole ping interval (cap*5). Bound = cap + 2 s margin catches the
     * regression while tolerating a loaded CI box. */
    CHECK(elapsed < cap + 2000U);
    /* Clean, app-requested disconnect must not deliver a transport callback. */
    CHECK_EQ_INT(g_st.disconnect_calls, 0);

    tai_ctx_deinit(ctx);
}

static const pal_t *observed_base_pal;
static pal_t observed_pal;
static size_t observed_recv_calls;
static size_t observed_recv_bytes;
static size_t observed_sleep_calls;
static size_t observed_empty_polls;
static size_t observed_after_bytes;
static size_t observed_blocking_after_bytes;
static size_t observed_nonblocking_after_bytes;
static uint32_t observed_max_timeout_after_bytes;

static int observed_recv(void *tcp, uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    pthread_mutex_lock(&g_st.mtx);
    observed_recv_calls++;
    if (observed_after_bytes && observed_recv_bytes >= observed_after_bytes) {
        if (timeout_ms) observed_blocking_after_bytes++;
        else observed_nonblocking_after_bytes++;
        if (timeout_ms > observed_max_timeout_after_bytes)
            observed_max_timeout_after_bytes = timeout_ms;
    }
    pthread_mutex_unlock(&g_st.mtx);

    int n = observed_base_pal->tcp_recv(tcp, buf, len, timeout_ms);

    pthread_mutex_lock(&g_st.mtx);
    if (n > 0) observed_recv_bytes += (size_t)n;
    pthread_mutex_unlock(&g_st.mtx);
    return n;
}

static int observed_poll(void *tcp, int events, uint32_t timeout_ms)
{
    pthread_mutex_lock(&g_st.mtx);
    if (events == 0) observed_empty_polls++;
    pthread_mutex_unlock(&g_st.mtx);
    return observed_base_pal->tcp_poll(tcp, events, timeout_ms);
}

static void observed_sleep(uint32_t ms)
{
    pthread_mutex_lock(&g_st.mtx);
    observed_sleep_calls++;
    pthread_mutex_unlock(&g_st.mtx);
    observed_base_pal->sleep_ms(ms);
}

static const pal_t *observe_pal(void)
{
    observed_base_pal = tai_pal_loopback();
    observed_pal = *observed_base_pal;
    observed_pal.tcp_recv = observed_recv;
    observed_pal.tcp_poll = observed_poll;
    observed_pal.sleep_ms = observed_sleep;
    observed_recv_calls = 0;
    observed_recv_bytes = 0;
    observed_sleep_calls = 0;
    observed_empty_polls = 0;
    observed_after_bytes = 0;
    observed_blocking_after_bytes = 0;
    observed_nonblocking_after_bytes = 0;
    observed_max_timeout_after_bytes = 0;
    return &observed_pal;
}

/* Pause after each delivered codec frame, including frames produced by one
 * Packet. Resume without new network bytes to prove no pending callback or
 * buffered Frame is stranded. */
static int flow_allowed_calls;
static int flow_paused_queries;
static uint64_t flow_resume_ms;

static int test_flow_control(tai_ctx_t *ctx, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&g_st.mtx);
    int ready = g_st.audio_calls < flow_allowed_calls;
    if (!ready) flow_paused_queries++;
    else if (flow_resume_ms == 0) flow_resume_ms = ctx->pal->time_ms();
    pthread_mutex_unlock(&g_st.mtx);
    return ready;
}

static size_t audio_frame_len_at(int index)
{
    return index < 160 ? g_st.audio_frame_lens[index]
                       : g_st.audio_frame_lens[159];
}

static void test_receive_backpressure(void)
{
    SECTION("receive_backpressure");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    flow_allowed_calls = 0;
    flow_paused_queries = 0;
    flow_resume_ms = 0;
    tai_config_t options = {
        .pal = observe_pal(),
        .on_flow_control = test_flow_control,
    };
    tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
    CHECK(ctx != NULL);
    if (!ctx) return;
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    CHECK(WAIT_FOR(flow_paused_queries > 0, 1000));

    pthread_mutex_lock(&g_st.mtx);
    size_t paused_recv_calls = observed_recv_calls;
    pthread_mutex_unlock(&g_st.mtx);

    static uint8_t audio[8][800];
    /* Distinct constant per Packet: a wrong slide lands in another Packet's
     * bytes and fails the packet-id check below regardless of position. */
    for (size_t packet = 0; packet < 8; packet++) {
        memset(audio[packet], (int)(0x10 + packet), sizeof(audio[packet]));
        CHECK(server_send_audio(ctx,
                                packet == 0 ? TAI_STREAM_START : TAI_STREAM_MIDDLE,
                                packet == 0 ? "111 1 16 16000 0 16000 20 40" : NULL,
                                audio[packet], sizeof(audio[packet]),
                                (uint16_t)(100 + packet)) > 0);
    }
    sleep_ms(100);
    pthread_mutex_lock(&g_st.mtx);
    CHECK_EQ_INT(observed_recv_calls, paused_recv_calls);
    CHECK_EQ_INT(g_st.audio_calls, 0);
    pthread_mutex_unlock(&g_st.mtx);

    for (int allowed = 1; allowed <= 8 * 20; allowed++) {
        pthread_mutex_lock(&g_st.mtx);
        flow_allowed_calls = allowed;
        pthread_mutex_unlock(&g_st.mtx);
        CHECK(WAIT_FOR(g_st.audio_calls >= allowed, 1000));
        sleep_ms(30);
        pthread_mutex_lock(&g_st.mtx);
        CHECK_EQ_INT(g_st.audio_calls, allowed);
        pthread_mutex_unlock(&g_st.mtx);
    }

    tai_disconnect(ctx);
    CHECK(observed_sleep_calls > 0);
    CHECK_EQ_INT(observed_empty_polls, 0);
    CHECK_EQ_INT(g_st.audio_calls, 160);
    CHECK_EQ_INT(g_st.audio_bytes, 8 * sizeof(audio[0]));
    for (int frame = 0; frame < 160; frame++)
        CHECK_EQ_INT(audio_frame_len_at(frame), 40);
    /* Byte-exact across ALL Packets, not just the paused first one: each
     * 800-byte Packet carries its own index in every byte, so any mis-slide
     * shows up as a wrong packet id. */
    for (size_t i = 0; i < g_st.audio_concat_len; i++) {
        size_t packet = i / sizeof(audio[0]);
        if (packet >= 8) break;
        CHECK_EQ_INT(g_st.audio_concat[i], audio[packet][0]);
    }
    CHECK_EQ_INT(g_st.disconnect_calls, 0);
    tai_ctx_deinit(ctx);
}

static size_t pending_remaining, pending_wire_len;
static uintptr_t pending_cursor;
static int pending_fragmented, pending_metadata_ok, pending_audio_ok;

static int pending_flow_control(tai_ctx_t *ctx, void *user_data)
{
    int ready = test_flow_control(ctx, user_data);
    pthread_mutex_lock(&g_st.mtx);
    if (!ready && ctx->rx_pending_wire_len) {
        pending_remaining = ctx->rx_pending_len;
        pending_wire_len = ctx->rx_pending_wire_len;
        pending_cursor = (uintptr_t)ctx->rx_pending_body;
        pending_metadata_ok = ctx->rx_audio_codec == TAI_AUDIO_OPUS &&
            ctx->rx_audio_sample_rate == 16000 && ctx->rx_audio_frame_duration == 20 &&
            ctx->rx_audio_frame_size == 40 && strcmp(ctx->rx_event_id, "pending-event") == 0;
    }
    pthread_mutex_unlock(&g_st.mtx);
    return ready;
}

static void pending_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    const uint8_t *storage = pending_fragmented ? ctx->frag_buf : ctx->rx_buf;
    uintptr_t p = (uintptr_t)msg->data;
    size_t size = pending_fragmented ? sizeof(ctx->frag_buf) : sizeof(ctx->rx_buf);
    pthread_mutex_lock(&g_st.mtx);
    pending_audio_ok &= p >= (uintptr_t)storage && p + msg->len <= (uintptr_t)storage + size &&
        msg->codec == TAI_AUDIO_OPUS && msg->sample_rate == 16000 &&
        msg->frame_duration == 20 && msg->stream_flag == TAI_STREAM_START &&
        msg->data_id == TAI_DATA_ID_AUDIO_DOWN && msg->timestamp_ms == 123456789 &&
        strcmp(msg->event_id, "pending-event") == 0;
    pthread_mutex_unlock(&g_st.mtx);
    on_audio(ctx, msg, ud);
}

/* A pinned Frame whose recorded wire length cannot be trusted must fail fast:
 * an oversized value would underflow rx_len into a huge memmove, and a zero one
 * would leave the Frame in place to be dispatched a second time. */
static void test_pending_bad_wire_len(void)
{
    SECTION("pending_audio_bad_wire_len");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    flow_allowed_calls = 0;
    flow_paused_queries = 0;
    flow_resume_ms = 0;
    pending_remaining = pending_wire_len = pending_cursor = 0;
    pending_fragmented = 0;
    pending_metadata_ok = 0;
    pending_audio_ok = 1;
    tai_config_t options = { .pal = observe_pal(),
                             .on_flow_control = pending_flow_control };
    tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
    CHECK(ctx != NULL);
    if (!ctx) return;
    ctx->on_audio = pending_audio;
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);

    uint8_t payload[8 + 130], app[256];
    CHECK_EQ_INT(tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                     TAI_STREAM_START, 123456789, payload, sizeof(payload)), 8);
    for (size_t i = 8; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
    tai_attr_t attrs[] = {
        tai_attr_strv(TAI_ATTR_AUDIO_PARAMS, "111 1 16 16000 0 16000 20 40"),
        tai_attr_strv(TAI_ATTR_EVENT_ID, "pending-event"),
    };
    int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_AUDIO, attrs, 2,
                                 payload, sizeof(payload), app, sizeof(app));
    CHECK(alen > 100);
    if (alen <= 100) { tai_disconnect(ctx); tai_ctx_deinit(ctx); return; }

    /* Admit one codec frame, then close admission: the Packet pauses mid-body. */
    pthread_mutex_lock(&g_st.mtx);
    flow_allowed_calls = 1;
    pthread_mutex_unlock(&g_st.mtx);
    uint16_t seq = 200;
    CHECK(server_send(ctx, TAI_PKT_AUDIO, attrs, 2, payload, sizeof(payload),
                      seq++) > 0);
    CHECK(WAIT_FOR(pending_wire_len > 0, 1000));

    /* Corrupt the recorded wire length, then let delivery run to completion. */
    pthread_mutex_lock(&g_st.mtx);
    ctx->rx_pending_wire_len = ctx->rx_len + 1;
    flow_allowed_calls = 100;
    pthread_mutex_unlock(&g_st.mtx);

    CHECK(WAIT_FOR(g_st.disconnect_calls >= 1, 1000));
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_PROTOCOL);
    CHECK_EQ_INT(g_st.disconnect_detail, TAI_PROTO_ERR_FRAME_DECODE);
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
}

static void test_pending_audio(void)
{
    for (int mode = 0; mode < 3; mode++) {
        SECTION(mode == 0 ? "pending_audio_multiple_pauses" :
                mode == 1 ? "pending_audio_fragmented" : "pending_audio_shutdown");
        static uint8_t ctx_mem[sizeof(struct tai_ctx)];
        flow_allowed_calls = 0;
        flow_paused_queries = 0;
        flow_resume_ms = 0;
        pending_remaining = pending_wire_len = pending_cursor = 0;
        pending_fragmented = mode != 0;
        pending_metadata_ok = 0;
        pending_audio_ok = 1;
        tai_config_t options = { .pal = observe_pal(), .on_flow_control = pending_flow_control };
        tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
        CHECK(ctx != NULL);
        if (!ctx) return;
        ctx->on_audio = pending_audio;
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        CHECK(WAIT_FOR(flow_paused_queries > 0, 1000));
        uint8_t payload[8 + 130], app[256];
        CHECK_EQ_INT(tai_pack_media_hdr(TAI_VER_21, TAI_DATA_ID_AUDIO_DOWN,
                         TAI_STREAM_START, 123456789, payload, sizeof(payload)), 8);
        for (size_t i = 8; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
        tai_attr_t attrs[] = {
            tai_attr_strv(TAI_ATTR_AUDIO_PARAMS, "111 1 16 16000 0 16000 20 40"),
            tai_attr_strv(TAI_ATTR_EVENT_ID, "pending-event"),
        };
        int alen = tai_packet_encode(TAI_VER_21, TAI_PKT_AUDIO, attrs, 2,
                                     payload, sizeof(payload), app, sizeof(app));
        CHECK(alen > 100);
        if (alen <= 100) { tai_disconnect(ctx); tai_ctx_deinit(ctx); return; }
        uint16_t seq = 100;
        size_t expected_wire_len;
        if (pending_fragmented) {
            size_t cuts[] = { 50, 100, (size_t)alen };
            server_send_app_fragmented(ctx, app, (size_t)alen, cuts, 3, &seq);
            expected_wire_len = (size_t)alen - 100 + 37;
        } else {
            expected_wire_len = server_send(ctx, TAI_PKT_AUDIO, attrs, 2,
                                            payload, sizeof(payload), seq++);
        }
        CHECK(server_send_event_id(ctx, TAI_EVT_END, "later-event", seq++) > 0);
        uintptr_t first_cursor = 0;
        for (int allowed = 1; allowed <= (mode == 2 ? 1 : 3); allowed++) {
            pthread_mutex_lock(&g_st.mtx);
            flow_allowed_calls = allowed;
            pthread_mutex_unlock(&g_st.mtx);
            CHECK(WAIT_FOR(pending_remaining == 130 - 40 * (size_t)allowed, 1000));
            pthread_mutex_lock(&g_st.mtx);
            CHECK_EQ_INT(g_st.audio_calls, allowed);
            CHECK_EQ_INT(g_st.event_calls, 0);
            CHECK_EQ_INT(pending_wire_len, expected_wire_len);
            CHECK(pending_metadata_ok);
            if (allowed == 1) first_cursor = pending_cursor;
            CHECK_EQ_INT(pending_cursor - first_cursor, 40 * (allowed - 1));
            size_t reads = observed_recv_calls;
            pthread_mutex_unlock(&g_st.mtx);
            sleep_ms(50);
            pthread_mutex_lock(&g_st.mtx);
            CHECK_EQ_INT(observed_recv_calls, reads);
            pthread_mutex_unlock(&g_st.mtx);
        }
        if (mode != 2) {
            pthread_mutex_lock(&g_st.mtx);
            flow_allowed_calls = 100;
            pthread_mutex_unlock(&g_st.mtx);
            CHECK(WAIT_FOR(g_st.event_calls == 1, 1000));
        }
        tai_disconnect(ctx);
        CHECK(pending_audio_ok);
        CHECK_EQ_INT(g_st.audio_calls, mode == 2 ? 1 : 4);
        CHECK_EQ_INT(g_st.audio_concat_len, mode == 2 ? 40 : 130);
        CHECK(memcmp(g_st.audio_concat, payload + 8, g_st.audio_concat_len) == 0);
        if (mode != 2) {
            CHECK_EQ_INT(g_st.audio_frame_lens[3], 10);
            CHECK(strcmp(g_st.last_event_event_id, "later-event") == 0);
        }
        CHECK_EQ_INT(ctx->rx_pending_len, 0);
        CHECK_EQ_INT(ctx->rx_pending_wire_len, 0);
        CHECK(ctx->rx_pending_body == NULL);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
        tai_ctx_deinit(ctx);
    }
}

static int eof_released, eof_hook_phase, eof_buffered_b;
static size_t eof_paused_queries, eof_frame_b_len;
static uint8_t eof_frame_b[256];

static int buffered_eof_flow_control(tai_ctx_t *ctx, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&g_st.mtx);
    int ready = eof_released;
    if (!ready) eof_paused_queries++;
    else if (g_st.text_calls == 1 && eof_hook_phase == 0) {
        /* This callback runs on the receive worker: inspecting its buffer here
         * is safe, unlike sampling rx_len from the test thread. */
        eof_buffered_b = ctx->rx_len == eof_frame_b_len &&
            memcmp(ctx->rx_buf, eof_frame_b, eof_frame_b_len) == 0;
        eof_hook_phase = 1;
        tai_loopback_close_connection();
        ready = 0;
    } else if (eof_hook_phase == 1) {
        eof_hook_phase = 2;  /* admission reopens on the very next query */
    }
    pthread_mutex_unlock(&g_st.mtx);
    return ready;
}

static void test_buffered_frame_before_eof(void)
{
    SECTION("buffered_frame_before_eof");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    eof_released = eof_hook_phase = eof_buffered_b = 0;
    eof_paused_queries = 0;
    tai_config_t options = { .pal = observe_pal(),
                             .on_flow_control = buffered_eof_flow_control };
    tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
    CHECK(ctx != NULL);
    if (!ctx) return;
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    CHECK(WAIT_FOR(eof_paused_queries > 0, 1000));

    uint8_t app[128], frames[512];
    int alen = build_text_app(ctx, TAI_STREAM_ONE_SHOT, 1, "A", 1,
                              app, sizeof(app));
    CHECK(alen > 0);
    int a = alen > 0 ? tai_frame_encode(TAI_FRAG_NONE, 10, app, (size_t)alen,
        ctx->sign_key, 32, ctx->pal, frames, sizeof(frames)) : -1;
    alen = build_text_app(ctx, TAI_STREAM_ONE_SHOT, 2, "B", 1,
                          app, sizeof(app));
    CHECK(alen > 0);
    int b = alen > 0 ? tai_frame_encode(TAI_FRAG_NONE, 11, app, (size_t)alen,
        ctx->sign_key, 32, ctx->pal, eof_frame_b, sizeof(eof_frame_b)) : -1;
    CHECK(a > 0 && b > 0);
    if (a <= 0 || b <= 0) { tai_disconnect(ctx); tai_ctx_deinit(ctx); return; }
    memcpy(frames + a, eof_frame_b, (size_t)b);
    pthread_mutex_lock(&g_st.mtx);
    eof_frame_b_len = (size_t)b;
    size_t before_bytes = observed_recv_bytes;
    tai_loopback_push_recv(frames, (size_t)(a + b));
    eof_released = 1;
    pthread_mutex_unlock(&g_st.mtx);

    CHECK(WAIT_FOR(g_st.disconnect_calls > 0, 2000));
    tai_disconnect(ctx);
    CHECK_EQ_INT(eof_hook_phase, 2);
    CHECK(eof_buffered_b);
    CHECK_EQ_INT(observed_recv_bytes - before_bytes, a + b);
    CHECK_EQ_INT(g_st.text_calls_at_disconnect, 2);
    CHECK(strcmp(g_st.text_buf, "AB") == 0);
    CHECK_EQ_INT(g_st.disconnect_calls, 1);
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_TRANSPORT);
    CHECK_EQ_INT(g_st.disconnect_detail, TAI_TRANSPORT_EOF);
    tai_ctx_deinit(ctx);
}

static void test_partial_frame_blocking_recv(void)
{
    const size_t cuts[] = { 2, 8 };  /* incomplete 5-byte header / incomplete body */
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        SECTION(i == 0 ? "partial_header_blocking_recv" : "partial_body_blocking_recv");
        static uint8_t ctx_mem[sizeof(struct tai_ctx)];
        tai_config_t options = { .pal = observe_pal(),
                                 .ping_interval_ms = 1000000 };
        tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
        CHECK(ctx != NULL);
        if (!ctx) return;
        tai_loopback_set_recv_cap_ms(AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS + 5000U);
        CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
        uint8_t app[128], frame[256];
        int alen = build_text_app(ctx, TAI_STREAM_ONE_SHOT, 1,
                                  "partial", 7, app, sizeof(app));
        CHECK(alen > 0);
        int flen = alen > 0 ? tai_frame_encode(TAI_FRAG_NONE, 10, app, (size_t)alen,
            ctx->sign_key, 32, ctx->pal, frame, sizeof(frame)) : -1;
        CHECK(flen > (int)cuts[i]);
        if (flen <= (int)cuts[i]) { tai_disconnect(ctx); tai_ctx_deinit(ctx); continue; }
        pthread_mutex_lock(&g_st.mtx);
        observed_after_bytes = observed_recv_bytes + cuts[i];
        pthread_mutex_unlock(&g_st.mtx);
        tai_loopback_push_recv(frame, cuts[i]);
        CHECK(WAIT_FOR(observed_recv_bytes >= observed_after_bytes, 1000));
        CHECK(WAIT_FOR(observed_blocking_after_bytes > 0, 1000));
        pthread_mutex_lock(&g_st.mtx);
        CHECK(observed_nonblocking_after_bytes <= 4);
        CHECK(observed_max_timeout_after_bytes <= AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS);
        CHECK_EQ_INT(g_st.text_calls, 0);
        pthread_mutex_unlock(&g_st.mtx);
        tai_loopback_push_recv(frame + cuts[i], (size_t)flen - cuts[i]);
        CHECK(WAIT_FOR(g_st.text_calls == 1, 1000));
        tai_disconnect(ctx);
        CHECK(strcmp(g_st.text_buf, "partial") == 0);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
        tai_ctx_deinit(ctx);
    }
}

static void test_long_receive_pause_liveness(void)
{
    SECTION("long_receive_pause_liveness");
    static uint8_t ctx_mem[sizeof(struct tai_ctx)];
    const uint32_t timeout_ms = 400;
    flow_allowed_calls = 0;
    flow_paused_queries = 0;
    flow_resume_ms = 0;
    tai_config_t options = {
        .pal = observe_pal(),
        .on_flow_control = test_flow_control,
        .ping_interval_ms = 100,
        .ping_timeout_ms = timeout_ms,
    };
    tai_ctx_t *ctx = setup_ctx_config(ctx_mem, &options);
    CHECK(ctx != NULL);
    if (!ctx) return;
    CHECK_EQ_INT(tai_connect(ctx), TAI_OK);
    CHECK(WAIT_FOR(flow_paused_queries > 0, 1000));

    sleep_ms(timeout_ms + 250);
    pthread_mutex_lock(&g_st.mtx);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);
    flow_allowed_calls = 100;
    pthread_mutex_unlock(&g_st.mtx);
    CHECK(WAIT_FOR(flow_resume_ms != 0, 1000));
    sleep_ms(100);
    pthread_mutex_lock(&g_st.mtx);
    CHECK_EQ_INT(g_st.disconnect_calls, 0);
    pthread_mutex_unlock(&g_st.mtx);

    uint8_t audio[40];
    memset(audio, 0x6b, sizeof(audio));
    uint64_t sent_ms = ctx->pal->time_ms();
    CHECK(server_send_audio(ctx, TAI_STREAM_START,
                            "111 1 16 16000 0 16000 20 40",
                            audio, sizeof(audio), 10) > 0);
    CHECK(WAIT_FOR(g_st.audio_calls == 1, 1000));
    CHECK(WAIT_FOR(g_st.disconnect_calls > 0, timeout_ms + 1500));
    tai_disconnect(ctx);
    CHECK_EQ_INT(g_st.disconnect_reason, TAI_DISCONNECT_TRANSPORT);
    CHECK_EQ_INT(g_st.disconnect_detail, TAI_TRANSPORT_PING_TIMEOUT);
    CHECK(g_st.disconnect_ms >= sent_ms + timeout_ms);
    tai_ctx_deinit(ctx);
}

static void check_pal_sleep(const pal_t *pal, void *tcp)
{
    uint8_t byte = 0;
    CHECK_EQ_INT(pal->tcp_poll(tcp, 1, 1000), 1);
    uint64_t start = pal->time_ms();
    pal->sleep_ms(120);
    uint64_t elapsed = pal->time_ms() - start;
    CHECK(elapsed >= 120);
    CHECK(elapsed < 2120);
    CHECK_EQ_INT(pal->tcp_poll(tcp, 1, 0), 1);
    CHECK_EQ_INT(pal->tcp_recv(tcp, &byte, 1, 0), 1);
    CHECK_EQ_INT(byte, 0x5a);
    start = pal->time_ms();
    pal->sleep_ms(0);
    CHECK(pal->time_ms() - start < 1000);
}

static void test_pal_sleep(void)
{
    SECTION("pal_sleep_required");
    pal_t incomplete = *tai_pal_loopback();
    CHECK(pal_is_valid(&incomplete));
    incomplete.sleep_ms = NULL;
    CHECK(!pal_is_valid(&incomplete));

    SECTION("pal_sleep_loopback");
    tai_loopback_reset();
    const pal_t *pal = tai_pal_loopback();
    void *tcp = pal->tcp_connect("loopback.test", 443, 1000);
    const uint8_t byte = 0x5a;
    tai_loopback_push_recv(&byte, 1);
    check_pal_sleep(pal, tcp);
    pal->tcp_close(tcp);

    SECTION("pal_sleep_posix");
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    if (listener < 0) return;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_EQ_INT(bind(listener, (struct sockaddr *)&addr, sizeof(addr)), 0);
    socklen_t addr_len = sizeof(addr);
    CHECK_EQ_INT(getsockname(listener, (struct sockaddr *)&addr, &addr_len), 0);
    CHECK_EQ_INT(listen(listener, 1), 0);
    pal = tai_pal_posix();
    tcp = pal->tcp_connect("127.0.0.1", ntohs(addr.sin_port), 1000);
    CHECK(tcp != NULL);
    if (tcp) {
        int peer = accept(listener, NULL, NULL);
        CHECK(peer >= 0);
        if (peer >= 0) {
            CHECK_EQ_INT(send(peer, &byte, 1, 0), 1);
            check_pal_sleep(pal, tcp);
            close(peer);
        }
        pal->tcp_close(tcp);
    }
    close(listener);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== Tuya AI -- integration tests (loopback PAL) ===\n");
    /* Logging is suppressed by default; set TAI_LOOPBACK_VERBOSE=1 to
     * enable -- the gate the loopback PAL's old lb_log handler applied,
     * now a mode default of this build's compile-time sink. */
    test_log_env_default();
    pthread_mutex_init(&g_st.mtx, NULL);

    test_event_user_data();
    test_pending_audio();
    test_pending_bad_wire_len();
    test_receive_backpressure();
    test_buffered_frame_before_eof();
    test_partial_frame_blocking_recv();
    test_long_receive_pause_liveness();
    test_pal_sleep();
    test_text_query();
    test_audio_roundtrip();
    test_image_query();
    test_image_recv();
    test_image_audio_query();
    test_disconnect_eof();
    test_session_close_then_transport();
    test_liveness_during_stream();
    test_request_disconnect_from_callback();
    test_event_id_latch();
    test_fail_fast();
    test_unknown_tolerated();
    test_media_audio_fragmented();
    test_media_audio_remainder();
    test_media_audio_large_frame();
    test_media_audio_two_streams();
    test_media_event_fragmented();
    test_media_reconnect_midstream();
    test_sg_audio_fragmented_uplink();
    test_sg_image_fragmented_uplink();
    test_sg_text_fragmented_uplink();
    test_sg_coalesce_boundary();
    test_sg_mcp_response();
    test_sg_send_failure();
    test_sg_control_json_limit();
    test_confirmed_connect();
    test_disconnect_latency();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    pthread_mutex_destroy(&g_st.mtx);
    return g_fail ? 1 : 0;
}
