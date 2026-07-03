/*
 * tai_client.c -- High-level Tuya AI client: connect, send, disconnect.
 *
 * This file owns:
 *   - tai_ctx_t memory layout  (struct defined in tai_internal.h)
 *   - Connection lifecycle:    tai_connect(), tai_disconnect()
 *   - Internal receive loop:   worker_thread (background thread)
 *   - All send helpers:        tai_send_text(), tai_send_audio_*(), etc.
 *
 * Threading model:
 *   tai_connect() automatically spawns a background thread that drives
 *   tai_poll() + keepalive pings.  tai_disconnect() stops and joins it.
 *   Send calls from any thread are mutex-protected.
 */

#include "tai_internal.h"
#include "rng.h"
#include <stdio.h>
#include <unistd.h>

#define TAG "client"

/* =========================================================================
 * Internal mutex helpers — pal validity is enforced at tai_ctx_init time.
 * ========================================================================= */
static inline void ctx_lock(tai_ctx_t *ctx)   { ctx->pal->mutex_lock(ctx->mutex); }
static inline void ctx_unlock(tai_ctx_t *ctx) { ctx->pal->mutex_unlock(ctx->mutex); }

void tai_emit_disconnect(tai_ctx_t *ctx, uint8_t reason,
                         uint8_t detail, uint16_t close_code)
{
    if (!ctx->on_disconnect) return;

    /* on_disconnect is documented to fire only on the worker thread. During
     * tai_connect's synchronous SessionNew-ack wait, tai_process_rx runs on the
     * CONNECTING thread, so a server SESSION_CLOSE dispatched there would fire
     * this for a connection the app never saw established. Suppress it while
     * connecting: tai_connect's return value is the signal for a failed/closed
     * handshake. */
    if (ctx->connecting) return;

    /* Connect-on-send: a terminal drop will be healed on the next tai_send_*, so
     * don't surface it — the send's own return value is the signal if the heal
     * later fails (no on_disconnect is fired for connect-on-send failures, which
     * keeps on_disconnect strictly a worker-thread callback). Non-terminal
     * SESSION_CLOSE still fires. */
    if (ctx->auto_reconnect && reason != TAI_DISCONNECT_SESSION_CLOSE) return;

    uint8_t alive = (reason == TAI_DISCONNECT_SESSION_CLOSE) ? 1 : 0;

    /* Single-point guarantee applies to TERMINAL disconnects only (the link is
     * truly going away): fire at most once. A server SESSION_CLOSE
     * (connection_alive=1) is a distinct, non-terminal event — the link may
     * persist for a new session — so it neither latches the guard nor is
     * suppressed by it. A real transport death that FOLLOWS a SESSION_CLOSE
     * must still be delivered. Re-armed by tai_connect()/tai_disconnect(). */
    if (!alive) {
        if (ctx->disconnect_emitted) return;
        ctx->disconnect_emitted = 1;
    }

    tai_disconnect_msg_t m = {0};
    m.reason           = reason;
    m.detail           = detail;
    m.close_code       = close_code;
    m.connection_alive = alive;
    size_t n = strlen(ctx->session_id);
    if (n >= sizeof(m.session_id)) n = sizeof(m.session_id) - 1;
    memcpy(m.session_id, ctx->session_id, n);
    m.session_id[n] = '\0';
    ctx->on_disconnect(ctx, &m, ctx->user_data);
}

/* Unified I/O helpers: dispatch to TLS or raw TCP based on disable_tls */
static inline int ctx_io_send(tai_ctx_t *ctx, const uint8_t *buf, size_t len)
{
    if (!ctx->disable_tls) {
        int r = tls_write(ctx->tls, buf, len, 10000);
        return (r == TLS_OK) ? TAI_OK : TAI_ERR_NET;
    }

    /* Raw TCP (test mode): blocking send with 3s total budget. */
    size_t written = 0;
    uint64_t start = ctx->pal->time_ms();
    const uint64_t limit_ms = 3000;
    while (written < len) {
        uint64_t elapsed = ctx->pal->time_ms() - start;
        if (elapsed >= limit_ms) return TAI_ERR_NET;
        int rc = ctx->pal->tcp_send(ctx->raw_tcp,
                                     buf + written, len - written,
                                     (uint32_t)(limit_ms - elapsed));
        if (rc > 0) { written += (size_t)rc; continue; }
        return TAI_ERR_NET;  /* TAI_ERR_AGAIN here = budget exhausted */
    }
    return TAI_OK;
}

static inline int ctx_io_recv(tai_ctx_t *ctx, uint8_t *buf, size_t buf_len,
                               uint32_t timeout_ms)
{
    if (ctx->disable_tls)
        return ctx->pal->tcp_recv(ctx->raw_tcp, buf, buf_len, timeout_ms);

    int n = tls_read(ctx->tls, buf, buf_len, timeout_ms);
    if (n == TLS_ERR_AGAIN) return TAI_ERR_AGAIN;
    if (n < 0) return TAI_ERR_NET;
    return n;
}

static inline void ctx_io_close(tai_ctx_t *ctx)
{
    if (ctx->disable_tls) {
        if (ctx->raw_tcp) { ctx->pal->tcp_close(ctx->raw_tcp); ctx->raw_tcp = NULL; }
    } else {
        if (ctx->tls) { tls_close(ctx->tls); ctx->tls = NULL; }
    }
}

/* Decode + log an outgoing application packet.  Separated from send_app
 * so the stack-allocated attr array only exists during the log call. */
static void log_send_packet(tai_ctx_t *ctx,
                            const uint8_t *app_bytes, size_t app_len)
{
    uint8_t     pkt_type;
    tai_attr_t  attrs[TAI_MAX_ATTRS];
    int         attr_count = 0;
    const uint8_t *payload;
    size_t         payload_len;
    if (tai_packet_decode(ctx->proto_ver, app_bytes, app_len,
                          &pkt_type, attrs, TAI_MAX_ATTRS, &attr_count,
                          &payload, &payload_len) == TAI_OK) {
        tai_log_packet(ctx->proto_ver, 1,
                       pkt_type, attrs, attr_count, payload, payload_len);
    }
}

/* =========================================================================
 * Scatter-gather streaming send (§6)
 *
 * A media packet's small application header is built by the caller into
 * tx_hdr_buf + 5 (leaving room for the 5-byte frame header); its large payload
 * stays in the caller's buffer. send_one_frame_sg emits one frame as 2–3 writes
 * (merged [frame header || app header], then zero-copy payload, then signature)
 * and send_app_sg fragments the logical hdr||payload concat across frames.
 *
 * The signature is computed BEFORE any byte goes on the wire (it samples the
 * caller's payload buffer directly, O(1) in payload size). The caller holds
 * ctx_lock for the whole packet, so the payload is stable between signing and
 * sending and frames never interleave with the worker's Ping.
 *
 * Atomicity (§6.3): a failure BEFORE the first write rolls back the sequence and
 * returns the build/arg error (nothing reached the wire). A failure AFTER the
 * first byte has hit the wire leaves a half-frame (declared length, truncated
 * bytes) and desyncs the stream unrecoverably — it returns TAI_ERR_NET. That is
 * a synchronous error the caller must act on: an app sender disconnects and
 * reconnects; the worker's own Ping turns a failed send into a TRANSPORT
 * disconnect on its next pass. There is no separate "broken" latch — the return
 * value is the signal, and TAI_ERR_NET specifically means bytes were committed
 * to the wire (so the sequence number is NOT rolled back).
 * ========================================================================= */
#define TAI_FRAME_HDR_LEN 5

/* Emit one frame. The application-header bytes (hdr_len of them) are already at
 * ctx->tx_hdr_buf + 5; hdr_len is 0 for non-first fragments. pay/pay_len is the
 * payload slice for this frame (zero-copy, from the caller's buffer). */
static int send_one_frame_sg(tai_ctx_t *ctx, uint8_t frag_flag, uint16_t seq,
                             size_t hdr_len,
                             const uint8_t *pay, size_t pay_len)
{
    size_t plen = hdr_len + pay_len;                 /* frame payload, excl sig */
    if (plen + ctx->sig_len > 0xFFFFU) return TAI_ERR_ARGS;

    uint8_t *head = ctx->tx_hdr_buf;                 /* [frame hdr 5][app hdr]  */
    head[0] = (uint8_t)(((frag_flag & 0x03) << 6) | (0x02 << 4) | 0x02);
    tai_w16(head + 1, seq);
    tai_w16(head + 3, (uint16_t)(plen + ctx->sig_len));
    size_t head_len = TAI_FRAME_HDR_LEN + hdr_len;   /* app hdr already in place */

    /* Compute the signature up front (over frame-hdr || app-hdr || payload). */
    if (ctx->sig_len > 0) {
        tai_seg_t segs[2] = { { head, head_len }, { pay, pay_len } };
        int rc = tai_frame_hmac_sg(segs, 2, ctx->sign_key, ctx->sig_len, ctx->tx_sig);
        if (rc != TAI_OK) return rc;                 /* pre-wire: recoverable */
    }

    /* From here on, any failure has committed bytes to the wire and desyncs the
     * stream: return TAI_ERR_NET (distinct from the pre-wire errors above so the
     * caller knows the sequence number was consumed). */
    int rc = ctx_io_send(ctx, head, head_len);
    if (rc != TAI_OK) return TAI_ERR_NET;
    if (pay_len) {
        rc = ctx_io_send(ctx, pay, pay_len);
        if (rc != TAI_OK) return TAI_ERR_NET;
    }
    if (ctx->sig_len > 0) {
        rc = ctx_io_send(ctx, ctx->tx_sig, ctx->sig_len);
        if (rc != TAI_OK) return TAI_ERR_NET;
    }
    return TAI_OK;
}

/* Stream an application packet whose header is at ctx->tx_hdr_buf+5 (hdr_len
 * bytes) and whose payload is `payload` (payload_len bytes). Caller holds
 * ctx_lock. The header never spans fragments (asserted). */
static int send_app_sg(tai_ctx_t *ctx, size_t hdr_len,
                       const uint8_t *payload, size_t payload_len)
{
    if (TAI_FRAME_HDR_LEN + hdr_len > sizeof(ctx->tx_hdr_buf)) return TAI_ERR_MEM;
    if (hdr_len >= TAI_MAX_FRAGMENT_PAYLOAD)                   return TAI_ERR_MEM;

    size_t total = hdr_len + payload_len;

    if (total <= TAI_MAX_FRAGMENT_PAYLOAD) {
        uint16_t seq = tai_next_seq(ctx);
        int rc = send_one_frame_sg(ctx, TAI_FRAG_NONE, seq,
                                   hdr_len, payload, payload_len);
        if (rc != TAI_OK && rc != TAI_ERR_NET) {     /* pre-wire: roll back seq */
            ctx->seq = (uint16_t)(seq - 1);
            if (ctx->seq == 0) ctx->seq = 0xFFFF;
        }
        return rc;
    }

    /* Fragment over the logical hdr||payload concat. The header lives only in
     * the first fragment (hdr_len < TAI_MAX_FRAGMENT_PAYLOAD guarantees the
     * whole header plus some payload fits the first chunk). */
    size_t offset = 0;
    while (offset < total) {
        size_t chunk = total - offset;
        if (chunk > TAI_MAX_FRAGMENT_PAYLOAD) chunk = TAI_MAX_FRAGMENT_PAYLOAD;
        uint8_t flag = (offset == 0)                ? TAI_FRAG_FIRST
                     : (offset + chunk >= total)    ? TAI_FRAG_LAST
                                                    : TAI_FRAG_MIDDLE;
        size_t hl; const uint8_t *p; size_t pl;
        if (offset == 0) {
            hl = hdr_len;
            p  = payload;
            pl = chunk - hdr_len;
        } else {
            hl = 0;
            p  = payload + (offset - hdr_len);
            pl = chunk;
        }
        uint16_t seq = tai_next_seq(ctx);
        int rc = send_one_frame_sg(ctx, flag, seq, hl, p, pl);
        if (rc != TAI_OK) {
            /* Reclaim the seq only if NOTHING of this packet reached the wire
             * yet (first fragment, pre-wire failure). Once an earlier fragment
             * is committed (offset>0), a later pre-wire failure still leaves a
             * partial packet on the wire, so the seq is spent and the stream is
             * desynced — propagate the error without rolling back. */
            if (rc != TAI_ERR_NET && offset == 0) {
                ctx->seq = (uint16_t)(seq - 1);
                if (ctx->seq == 0) ctx->seq = 0xFFFF;
            }
            return rc;
        }
        offset += chunk;
    }
    return TAI_OK;
}

/* =========================================================================
 * Internal: send a complete (already-serialised) application packet.
 * Caller must hold ctx_lock().
 *
 * Control packets stream through the SAME scatter-gather sender as media: the
 * whole application packet is the (zero-copy) payload with no application
 * header (hdr_len=0), so there is no separate contiguous frame buffer. The 5-
 * byte frame header is built in tx_hdr_buf; the packet is sent as 2-3 writes.
 * Sequence rollback (pre-wire) is handled by send_app_sg; a mid-wire desync is
 * returned as TAI_ERR_NET (§6.3) for the caller to act on. ClientHello's
 * *unsigned* one-shot frame is the exception — it is framed inline in
 * tai_connect (sig_len=0). */
static int send_app(tai_ctx_t *ctx, const uint8_t *app_bytes, size_t app_len)
{
    if (log_get_level() >= LOG_INFO)
        log_send_packet(ctx, app_bytes, app_len);
    return send_app_sg(ctx, /*hdr_len=*/0, app_bytes, app_len);
}

/* =========================================================================
 * tai_ctx_size / tai_ctx_init / tai_ctx_deinit
 * ========================================================================= */
size_t tai_ctx_size(void)
{
    return sizeof(struct tai_ctx);
}

tai_ctx_t *tai_ctx_init(void *mem, const tai_config_t *cfg)
{
    if (!mem || !cfg || !pal_is_valid(cfg->pal)) return NULL;

    memset(mem, 0, sizeof(struct tai_ctx));
    tai_ctx_t *ctx = (tai_ctx_t *)mem;

    ctx->pal         = cfg->pal;
    ctx->host        = cfg->host;
    ctx->port        = cfg->port;
    ctx->tls_sni     = cfg->tls_sni;
    ctx->device_id   = cfg->device_id;
    ctx->local_key   = cfg->local_key;
    ctx->client_id   = cfg->device_id;

    ctx->proto_ver   = cfg->protocol_version ? cfg->protocol_version : TAI_VER_21;
    ctx->client_type = cfg->client_type       ? cfg->client_type      : TAI_CLIENT_DEVICE;
    ctx->biz_code    = cfg->biz_code          ? cfg->biz_code         : 65537U;
    ctx->biz_tag     = cfg->biz_tag           ? cfg->biz_tag          : 119U;
    ctx->sign_level  = cfg->sign_level        ? cfg->sign_level       : TAI_SIGN_HMAC_SHA256;

    switch (ctx->sign_level) {
        case TAI_SIGN_HMAC_SHA1:   ctx->sig_len = 20; break;
        case TAI_SIGN_HMAC_SHA256: ctx->sig_len = 32; break;
        default:                   ctx->sig_len =  0; break;
    }

    ctx->session_attrs_json    = cfg->session_attrs_json;
    ctx->event_user_data_json  = cfg->event_user_data_json;
    ctx->agent_token           = cfg->agent_token;
    ctx->on_audio              = cfg->on_audio;
    ctx->on_text               = cfg->on_text;
    ctx->on_image              = cfg->on_image;
    ctx->on_event              = cfg->on_event;
    ctx->on_disconnect         = cfg->on_disconnect;
    ctx->user_data             = cfg->user_data;

    ctx->ping_interval_ms  = cfg->ping_interval_ms  ? cfg->ping_interval_ms  : 60000U;
    ctx->ping_timeout_ms   = cfg->ping_timeout_ms   ? cfg->ping_timeout_ms   : 90000U;
    ctx->connect_timeout_ms = cfg->connect_timeout_ms ? cfg->connect_timeout_ms : 5000U;
    ctx->disable_tls      = cfg->disable_tls;
    ctx->cert_bundle_attach = cfg->cert_bundle_attach;
    ctx->auto_reconnect   = cfg->auto_reconnect;

    /* Initialise mutexes */
    ctx->mutex = cfg->pal->mutex_create();
    if (!ctx->mutex) return NULL;
    ctx->reconnect_mutex = cfg->pal->mutex_create();
    if (!ctx->reconnect_mutex) {
        ctx->pal->mutex_destroy(ctx->mutex);
        return NULL;
    }

    /* Seed the shared crypto RNG once, here at construction (single-threaded,
     * before tai_connect() spawns the receive thread). */
    if (rng_init(ctx->pal) != 0) {
        TAI_LOGE(ctx->pal, TAG, "ctx_init: rng_init failed");
        ctx->pal->mutex_destroy(ctx->mutex);
        ctx->pal->mutex_destroy(ctx->reconnect_mutex);
        return NULL;
    }

    TAI_LOGI(ctx->pal, TAG, "ctx_init: proto=v%u client_type=%s sign_level=%s sig_len=%u",
             ctx->proto_ver,
             tai_client_type_name(ctx->client_type),
             tai_sign_level_name(ctx->sign_level),
             ctx->sig_len);

    return ctx;
}

void tai_ctx_deinit(tai_ctx_t *ctx)
{
    if (!ctx) return;
    ctx_io_close(ctx);
    if (ctx->mutex) { ctx->pal->mutex_destroy(ctx->mutex); ctx->mutex = NULL; }
    if (ctx->reconnect_mutex) {
        ctx->pal->mutex_destroy(ctx->reconnect_mutex);
        ctx->reconnect_mutex = NULL;
    }
}

/* =========================================================================
 * tai_connect
 *
 * 1. Generate encrypt_random
 * 2. Derive encryption and signing keys
 * 3. TLS connect
 * 4. Send ClientHello (unencrypted, sig_len=0)
 * 5. Send SessionNew
 * 6. Start background receive thread
 * ========================================================================= */

/* Forward declarations (defined below; used by the confirmed-connect wait) */
static void *worker_thread(void *arg);
static int   tai_recv_data(tai_ctx_t *ctx, uint32_t timeout_ms);
static int   tai_process_rx(tai_ctx_t *ctx);

int tai_connect(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->pal) return TAI_ERR_ARGS;
    if (!ctx->host || ctx->port == 0) return TAI_ERR_ARGS;

    int rc;
    ctx->disconnect_emitted = 0;   /* re-arm single-point on_disconnect for this connection */

    TAI_LOGI(ctx->pal, TAG, "connecting to %s:%u", ctx->host, ctx->port);

    /* 1. Generate 32-byte random for security-suit */
    rc = tai_random_bytes(ctx->pal, ctx->encrypt_random, 32);
    if (rc != 0) {
        TAI_LOGE(ctx->pal, TAG, "random_bytes failed: %d", rc);
        return TAI_ERR_CRYPTO;
    }

    /* 2. Derive keys */
    const char *ikm    = ctx->local_key ? ctx->local_key : "";
    size_t      ikm_len = ctx->local_key ? strlen(ctx->local_key) : 0;

    rc = tai_crypto_derive_keys(ctx->proto_ver,
                                 (const uint8_t *)ikm, ikm_len,
                                 ctx->encrypt_random, 32,
                                 ctx->encrypt_key, ctx->sign_key,
                                 ctx->pal);
    if (rc != TAI_OK) {
        TAI_LOGE(ctx->pal, TAG, "key derivation failed: %d", rc);
        return rc;
    }
    TAI_LOGD(ctx->pal, TAG, "keys derived (ikm_len=%zu)", ikm_len);

    /* 3. TLS connect (or raw TCP in test mode) */
    if (ctx->disable_tls) {
        ctx->raw_tcp = ctx->pal->tcp_connect(ctx->host, ctx->port, ctx->connect_timeout_ms);
        if (!ctx->raw_tcp) {
            TAI_LOGE(ctx->pal, TAG, "TCP connect failed to %s:%u", ctx->host, ctx->port);
            return TAI_ERR_NET;
        }
        TAI_LOGI(ctx->pal, TAG, "TCP connected (disable_tls mode)");
    } else {
        tls_config_t tcfg = {
            .host                 = ctx->host,
            .port                 = ctx->port,
            .sni                  = ctx->tls_sni,
            .verify               = TLS_VERIFY_OPTIONAL,  /* fallback when no cert bundle */
            .cert_bundle_attach   = ctx->cert_bundle_attach,
            .connect_timeout_ms   = ctx->connect_timeout_ms, /* bounds TCP connect + TLS handshake */
            .pal                  = ctx->pal,
        };
        ctx->tls = tls_connect(&tcfg);
        if (!ctx->tls) {
            TAI_LOGE(ctx->pal, TAG, "TLS connect failed to %s:%u", ctx->host, ctx->port);
            return TAI_ERR_TLS;
        }
        TAI_LOGI(ctx->pal, TAG, "TLS connected");
    }

    /* 4. ClientHello — sent UNENCRYPTED (sig_len = 0). It is the one unsigned
     * frame, so it can't go through send_app (which signs); it is small (no
     * JSON) and one-shot, so frame it inline on the stack. */
    int app_len = tai_proto_build_client_hello(ctx,
                                                ctx->tx_ctrl_buf,
                                                sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) {
        TAI_LOGE(ctx->pal, TAG, "build ClientHello failed: %d", app_len);
        tai_disconnect(ctx); return app_len;
    }

    uint16_t seq = tai_next_seq(ctx);
    /* 5-byte frame hdr + the ClientHello app block (built into tx_ctrl_buf, so
     * bounded by TAI_TX_CTRL_BUF_SIZE). Sized to that bound rather than a fixed
     * 256 so a long client_id/device_id frames fine — the only limit is the
     * same tx_ctrl_buf that the build step already enforces. Unsigned, so no
     * signature trailer. */
    uint8_t ch_frame[TAI_FRAME_HDR_LEN + TAI_TX_CTRL_BUF_SIZE];
    int frame_len = tai_frame_encode(TAI_FRAG_NONE, seq,
                                      ctx->tx_ctrl_buf, (size_t)app_len,
                                      ctx->sign_key, 0,  /* sig_len=0 */
                                      ctx->pal,
                                      ch_frame, sizeof(ch_frame));
    if (frame_len < 0) { tai_disconnect(ctx); return frame_len; }

    rc = ctx_io_send(ctx, ch_frame, (size_t)frame_len);
    if (rc != TAI_OK) {
        TAI_LOGE(ctx->pal, TAG, "send ClientHello failed: %d", rc);
        tai_disconnect(ctx); return rc;
    }
    if (log_get_level() >= LOG_INFO)
        log_send_packet(ctx, ctx->tx_ctrl_buf, (size_t)app_len);

    /* 5. SessionNew */
    app_len = tai_proto_build_session_new(ctx,
                                           ctx->tx_ctrl_buf,
                                           sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) {
        TAI_LOGE(ctx->pal, TAG, "build SessionNew failed: %d", app_len);
        tai_disconnect(ctx); return app_len;
    }

    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) {
        TAI_LOGE(ctx->pal, TAG, "send SessionNew failed: %d", rc);
        tai_disconnect(ctx); return rc;
    }

    /* 5b. Confirmed connect: receive + process synchronously (the worker is not
     * started yet) until the server's SessionNew ack arrives, then require
     * status OK. Any timeout / EOF / recv error / fail-fast aborts the connect
     * and the app retries (reconnect is just another tai_connect). */
    ctx->session_ack = -1;
    ctx->connecting  = 1;   /* dispatch runs on THIS thread now: suppress on_disconnect */
    {
        uint64_t deadline = ctx->pal->time_ms() + ctx->connect_timeout_ms;
        while (ctx->session_ack < 0) {
            uint64_t now = ctx->pal->time_ms();
            if (now >= deadline) {
                TAI_LOGE(ctx->pal, TAG, "connect: SessionNew ack timeout");
                tai_disconnect(ctx); return TAI_ERR_NET;
            }
            int n = tai_recv_data(ctx, (uint32_t)(deadline - now));
            if (n == 0) {
                TAI_LOGE(ctx->pal, TAG, "connect: EOF before SessionNew ack");
                tai_disconnect(ctx); return TAI_ERR_NET;
            }
            if (n < 0 && n != TAI_ERR_AGAIN) {
                TAI_LOGE(ctx->pal, TAG, "connect: recv error %d", n);
                tai_disconnect(ctx); return TAI_ERR_NET;
            }
            if (n > 0) {
                int fatal = tai_process_rx(ctx);
                if (fatal != TAI_OK) {
                    TAI_LOGE(ctx->pal, TAG, "connect: protocol error before ack (%d)", fatal);
                    tai_disconnect(ctx); return TAI_ERR_PROTO;
                }
            }
        }
    }
    ctx->connecting = 0;   /* handshake done; the worker (started below) owns callbacks now */
    if (ctx->session_ack != 0) {
        TAI_LOGE(ctx->pal, TAG, "connect: server rejected session (status=%d)",
                 ctx->session_ack);
        tai_disconnect(ctx); return TAI_ERR_PROTO;
    }

    ctx->connected    = 1;
    ctx->session_open = 1;

    /* 6. Start background receive thread */
    ctx->last_ping_ms = ctx->pal->time_ms();
    ctx->last_pong_ms = ctx->last_ping_ms;
    ctx->last_rx_ms   = ctx->last_ping_ms;
    ctx->running      = 1;
    int trc = ctx->pal->thread_create(&ctx->thread_handle, worker_thread, ctx);
    if (trc != 0) {
        TAI_LOGE(ctx->pal, TAG, "worker thread create failed: %d", trc);
        ctx->running = 0;
        tai_disconnect(ctx);
        return TAI_ERR_MEM;
    }
    TAI_LOGD(ctx->pal, TAG, "worker thread started (ping_interval=%ums)",
             ctx->ping_interval_ms);

    TAI_LOGI(ctx->pal, TAG, "connected successfully");
    return TAI_OK;
}

/* =========================================================================
 * tai_request_disconnect -- ask the worker to stop, WITHOUT joining it.
 *
 * Safe from any thread, including inside a receive callback (which runs on the
 * worker thread). Unlike tai_disconnect() it does not join, so it can never
 * self-deadlock. The owning thread must still call tai_disconnect() afterwards
 * to join the worker and release resources.
 * ========================================================================= */
void tai_request_disconnect(tai_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->running = 0;   /* volatile; the worker exits its loop on the next pass */
}

/* =========================================================================
 * tai_disconnect
 * ========================================================================= */
void tai_disconnect(tai_ctx_t *ctx)
{
    if (!ctx) return;

    TAI_LOGI(ctx->pal, TAG, "disconnecting");

    /* Stop and join the background worker FIRST. With running=0 the worker exits
     * its next loop pass -- bounded by TAI_WORKER_POLL_CAP_MS (~200 ms) even when
     * idle, so this no longer blocks up to a whole ping interval. Joining BEFORE
     * we send SessionClose / close the socket is deliberate: it guarantees the
     * worker can neither (a) race our send on the shared TX buffers, nor (b)
     * observe the EOF the server may send in response to SessionClose and fire a
     * spurious on_disconnect(TRANSPORT) for what is a clean, app-requested
     * shutdown (a clean stop leaves f_reason == 0xFF, so no callback fires). */
    ctx->running = 0;
    if (ctx->thread_handle) {
        ctx->pal->thread_join(ctx->thread_handle);
        ctx->thread_handle = NULL;
        TAI_LOGD(ctx->pal, TAG, "worker thread joined");
    }

    /* Hold ctx_lock across SessionClose AND the I/O close. A concurrent app
     * sender on another thread only ever touches the socket under ctx_lock, so
     * taking it here guarantees we never free ctx->tls while a send is in
     * flight. ctx_io_close() nulls ctx->tls / raw_tcp under the same lock, so a
     * sender that acquires the lock *after* us reads NULL and fails gracefully
     * (tls_write(NULL) -> TLS_ERR_ARGS -> TAI_ERR_NET) instead of using freed
     * memory. The worker is already joined above, so it is not a concern here. */
    ctx_lock(ctx);

    /* SessionClose (best-effort) */
    if (ctx->connected && ctx->session_open && (ctx->tls || ctx->raw_tcp)) {
        int len = tai_proto_build_session_close(ctx,
                                                 ctx->tx_ctrl_buf,
                                                 sizeof(ctx->tx_ctrl_buf));
        if (len > 0)
            send_app(ctx, ctx->tx_ctrl_buf, (size_t)len);
        ctx->session_open = 0;
    }

    if (ctx->tls || ctx->raw_tcp) {
        ctx_io_close(ctx);
    }

    ctx->connected = 0;
    ctx->event_open = 0;
    ctx->rx_len     = 0;
    ctx->frag_len   = 0;
    ctx->frag_state = 0;
    ctx->connecting = 0;          /* clear in case a connect aborted mid-handshake */
    ctx->disconnect_emitted = 0;  /* re-arm the single-point on_disconnect      */
    ctx->rx_event_id[0] = '\0';   /* clear latched turn id so a reconnect starts clean */

    ctx_unlock(ctx);
}

/* =========================================================================
 * Internal: process one reassembled application packet
 * ========================================================================= */
static int process_app_packet(tai_ctx_t *ctx,
                               const uint8_t *app_bytes, size_t app_len)
{
    uint8_t     pkt_type;
    tai_attr_t  attrs[TAI_MAX_ATTRS];
    int         attr_count = 0;
    const uint8_t *payload;
    size_t         payload_len;

    int rc = tai_packet_decode(ctx->proto_ver,
                                app_bytes, app_len,
                                &pkt_type,
                                attrs, TAI_MAX_ATTRS, &attr_count,
                                &payload, &payload_len);
    if (rc != TAI_OK) {
        TAI_LOGW(ctx->pal, TAG, "packet decode failed: %d (app_len=%zu)", rc, app_len);
        return TAI_PROTO_ERR_PKT_DECODE;   /* fatal cause, returned to the worker */
    }

    tai_log_packet(ctx->proto_ver, 0,
                   pkt_type, attrs, attr_count, payload, payload_len);

    /* Returns TAI_OK, a TAI_PROTO_ERR_* detail, or TAI_RX_PEER_CLOSE|code. */
    return tai_proto_dispatch(ctx, pkt_type,
                               attrs, attr_count,
                               payload, payload_len);
}


/* =========================================================================
 * tai_recv_data  (internal, lock-free)
 *
 * Read data from TLS into rx_buf.  Only the worker thread ever touches
 * rx_buf, so no mutex is required.
 * Returns bytes read (>0), 0 on EOF, or a negative TAI_ERR_* code.
 * ========================================================================= */
static int tai_recv_data(tai_ctx_t *ctx, uint32_t timeout_ms)
{
    size_t space = sizeof(ctx->rx_buf) - ctx->rx_len;
    if (space == 0) return TAI_ERR_AGAIN;

    int n = ctx_io_recv(ctx,
                                ctx->rx_buf + ctx->rx_len,
                                space,
                                timeout_ms);
    if (n > 0) {
        ctx->rx_len   += (size_t)n;
        ctx->last_rx_ms = ctx->pal->time_ms();  /* inbound traffic = liveness */
        return n;
    }
    if (n == 0) return 0;                 /* EOF */
    return n;                             /* TAI_ERR_AGAIN or TAI_ERR_NET */
}

/* =========================================================================
 * tai_process_rx  (internal, worker-thread only)
 *
 * Decode complete frames in rx_buf, reassemble fragments, dispatch.
 * rx_buf / frag_buf are touched only by the worker thread, so no mutex
 * is required.  Dispatch invokes user callbacks while holding NO lock; if a
 * callback calls back into tai_send_*(), that acquires ctx_lock fresh (this
 * thread holds none here) and serialises against other senders normally.
 * Returns a fail-fast cause for the worker: TAI_OK (no fatal; processed zero or
 * more frames), a TAI_PROTO_ERR_* detail on a protocol error, or
 * TAI_RX_PEER_CLOSE|code on a server CONNECTION_CLOSE. Never touches the
 * connection lifecycle itself — the worker owns that.
 * ========================================================================= */
static int tai_process_rx(tai_ctx_t *ctx)
{
    /* Process all complete frames sitting in rx_buf. Any structural error is
     * fail-fast: on a reliable, ordered TLS stream a desync cannot be recovered
     * by dropping bytes/frames, so we RETURN the cause and let the worker tear
     * the connection down (the app reconnects). */
    while (ctx->rx_len >= 5) {
        /* Detect version from first byte */
        int ver = tai_frame_detect_version(ctx->rx_buf[0]);
        if (ver < 0) {
            TAI_LOGW(ctx->pal, TAG, "unknown frame byte 0x%02x", ctx->rx_buf[0]);
            return TAI_PROTO_ERR_BAD_VERSION;
        }

        /* How large is the complete frame? */
        size_t needed = tai_frame_total_size(ctx->rx_buf, ctx->rx_len);
        /* rx_buf is sized to one max fragment (TAI_RX_BUF_SIZE). A frame that
         * declares more than that can never be assembled — without this guard
         * the loop would break forever (needed > rx_len always) and recv would
         * return AGAIN until the ~90 s liveness timeout. Fail-fast instead so the
         * app reconnects immediately and the cause is visible. This only trips if
         * the server ignores the advertised MAX_FRAGMENT_LEN. */
        if (needed > sizeof(ctx->rx_buf)) {
            TAI_LOGE(ctx->pal, TAG,
                     "inbound frame %zu B exceeds rx_buf %zu B (server ignored MAX_FRAGMENT_LEN?)",
                     needed, sizeof(ctx->rx_buf));
            return TAI_PROTO_ERR_OVERSIZED;
        }
        if (needed == 0 || ctx->rx_len < needed) break;  /* need more data */

        /* Verify HMAC signature */
        int rc = tai_frame_verify(ctx->rx_buf, needed,
                                   ctx->sig_len, ctx->sign_key, ctx->pal);
        if (rc != TAI_OK) {
            TAI_LOGW(ctx->pal, TAG, "HMAC verify failed (frame=%zu bytes)", needed);
            return TAI_PROTO_ERR_HMAC;
        }

        /* Decode frame header */
        uint8_t        frag_flag;
        uint16_t       seq;
        const uint8_t *payload;
        size_t         payload_len;

        rc = tai_frame_decode(ctx->rx_buf, needed,
                               ctx->sig_len,
                               &frag_flag, &seq,
                               &payload, &payload_len);
        if (rc != TAI_OK)
            return TAI_PROTO_ERR_FRAME_DECODE;

        /* Reassemble fragments. FRAG_NONE is a complete packet; FIRST/MIDDLE/
         * LAST accumulate into frag_buf until LAST, then the whole packet is
         * dispatched. Dispatch happens BEFORE the memmove below consumes the
         * frame from rx_buf, so a FRAG_NONE packet's payload (which points into
         * rx_buf) stays valid for the call. Any structural error is fail-fast:
         * RETURN the cause (skipping the memmove) and let the worker tear the
         * connection down. */
        int            complete  = 0;
        const uint8_t *app_bytes = NULL;
        size_t         app_len   = 0;

        if (frag_flag == TAI_FRAG_NONE) {
            app_bytes = payload;
            app_len   = payload_len;
            complete  = 1;
        } else if (frag_flag == TAI_FRAG_FIRST) {
            ctx->frag_len   = 0;
            ctx->frag_state = 1;
            if (payload_len > sizeof(ctx->frag_buf))
                return TAI_PROTO_ERR_FRAG;                 /* oversized first fragment */
            memcpy(ctx->frag_buf, payload, payload_len);
            ctx->frag_len = payload_len;
        } else if (frag_flag == TAI_FRAG_MIDDLE) {
            if (ctx->frag_state != 1 ||
                ctx->frag_len + payload_len > sizeof(ctx->frag_buf))
                return TAI_PROTO_ERR_FRAG;                 /* orphan / overflow */
            memcpy(ctx->frag_buf + ctx->frag_len, payload, payload_len);
            ctx->frag_len += payload_len;
        } else { /* TAI_FRAG_LAST */
            if (ctx->frag_state != 1 ||
                ctx->frag_len + payload_len > sizeof(ctx->frag_buf))
                return TAI_PROTO_ERR_FRAG;                 /* orphan / overflow */
            memcpy(ctx->frag_buf + ctx->frag_len, payload, payload_len);
            ctx->frag_len += payload_len;
            app_bytes = ctx->frag_buf;
            app_len   = ctx->frag_len;
            complete  = 1;
            ctx->frag_state = 0;
        }

        if (complete && app_bytes && app_len > 0) {
            int fatal = process_app_packet(ctx, app_bytes, app_len);
            if (fatal != TAI_OK)
                return fatal;   /* PROTOCOL detail or TAI_RX_PEER_CLOSE|code */
        }

        /* Consume frame from rx_buf */
        memmove(ctx->rx_buf, ctx->rx_buf + needed, ctx->rx_len - needed);
        ctx->rx_len -= needed;
    }

    return TAI_OK;
}

/* =========================================================================
 * tai_ping (internal — called only by the worker thread)
 * ========================================================================= */
static int tai_ping(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    ctx_lock(ctx);
    int len = tai_proto_build_ping(ctx, ctx->tx_ctrl_buf, sizeof(ctx->tx_ctrl_buf));
    int rc = (len > 0) ? send_app(ctx, ctx->tx_ctrl_buf, (size_t)len) : len;
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_text
 *
 * Complete text query: EventStart → Text(OneShot) → EventPayloadsEnd → EventEnd
 * ========================================================================= */
/* Body of tai_send_text; ctx_lock held, args already validated. Returns on the
 * first error — the wrapper releases the lock once. */
static int send_text_locked(tai_ctx_t *ctx, const char *text, size_t len)
{
    int rc, app_len, hdr_len;

    /* EventStart (control, contiguous) */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_ctrl_buf,
                                           sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;
    ctx->event_open = 1;

    /* Text OneShot (scatter-gather: small header + zero-copy text payload) */
    hdr_len = tai_proto_build_text_hdr(ctx, TAI_DATA_ID_TEXT_UP,
                                       TAI_STREAM_ONE_SHOT, 0,
                                       ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                       sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    if (hdr_len < 0) return hdr_len;
    rc = send_app_sg(ctx, (size_t)hdr_len, (const uint8_t *)text, len);
    if (rc != TAI_OK) return rc;

    /* EventPayloadsEnd (control) */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_TEXT_UP,
                                                  ctx->tx_ctrl_buf,
                                                  sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;

    /* EventEnd (control) */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_ctrl_buf,
                                         sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    ctx->event_open = 0;
    return rc;
}

/* Connect-on-send gate for the stream-STARTING public sends. App-thread only.
 *   - link already up            -> TAI_OK
 *   - down, auto_reconnect off    -> TAI_ERR_ARGS (fail-fast, same as pre-feature)
 *   - down, auto_reconnect on     -> tai_disconnect() (join the exited worker +
 *       close the stale socket + reset) then tai_connect() (re-establish within
 *       connect_timeout_ms + new worker). On failure return that error so the
 *       send fails; no on_disconnect is fired (the return value is the signal).
 * Serialized by reconnect_mutex so concurrent down-link sends from multiple app
 * threads don't race the join/connect. NOT ctx->mutex: tai_disconnect/tai_connect
 * take that internally. MUST run on an app thread (it joins the worker), never a
 * receive callback.
 *
 * Note: only stream-starting sends use this. Mid-stream continuations
 * (tai_send_audio_chunk / tai_send_audio_end) require an already-open event that
 * a fresh reconnected session doesn't have, so they fail-fast on a down link
 * instead of silently emitting orphan frames -- the app must restart the stream
 * with tai_send_audio_start. */
static int ensure_connected(tai_ctx_t *ctx)
{
    if (ctx->connected) return TAI_OK;
    if (!ctx->auto_reconnect) return TAI_ERR_ARGS;

    ctx->pal->mutex_lock(ctx->reconnect_mutex);
    int rc = TAI_OK;
    if (!ctx->connected) {   /* re-check: another thread may have reconnected */
        TAI_LOGI(ctx->pal, TAG, "auto-reconnect: link down, re-establishing");
        tai_disconnect(ctx);
        rc = tai_connect(ctx);
        if (rc != TAI_OK)
            TAI_LOGE(ctx->pal, TAG, "auto-reconnect failed: %d", rc);
    }
    ctx->pal->mutex_unlock(ctx->reconnect_mutex);
    return rc;
}

int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len)
{
    if (!ctx || !text) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    TAI_LOGI(ctx->pal, TAG, "send_text: %zu bytes", len);
    ctx_lock(ctx);
    int rc = send_text_locked(ctx, text, len);
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_audio_start / _chunk / _end
 * ========================================================================= */
static int send_audio_start_locked(tai_ctx_t *ctx,
                                   uint8_t codec, uint8_t channels,
                                   uint8_t bit_depth, uint32_t sample_rate)
{
    /* Store audio params — first chunk will send with START flag */
    ctx->audio_codec       = codec;
    ctx->audio_channels    = channels;
    ctx->audio_bit_depth   = bit_depth;
    ctx->audio_sample_rate = sample_rate;
    ctx->audio_started     = 0;

    int app_len = tai_proto_build_event_start(ctx, ctx->tx_ctrl_buf,
                                               sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    int rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc == TAI_OK) ctx->event_open = 1;
    return rc;
}

int tai_send_audio_start(tai_ctx_t *ctx,
                          uint8_t codec, uint8_t channels,
                          uint8_t bit_depth, uint32_t sample_rate)
{
    if (!ctx) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    TAI_LOGI(ctx->pal, TAG, "audio_start: codec=%u ch=%u bits=%u rate=%u",
             codec, channels, bit_depth, sample_rate);
    ctx_lock(ctx);
    int rc = send_audio_start_locked(ctx, codec, channels, bit_depth, sample_rate);
    ctx_unlock(ctx);
    return rc;
}

int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len)
{
    /* Mid-stream continuation: fail-fast on a down link (no connect-on-send).
     * A reconnected session has no open audio event, so healing here would emit
     * an orphan MIDDLE frame; the app must restart via tai_send_audio_start. */
    if (!ctx || !ctx->connected || !pcm || len == 0) return TAI_ERR_ARGS;
    ctx_lock(ctx);

    /* First chunk carries START flag + codec params; rest are MIDDLE */
    uint8_t flag;
    uint8_t codec, ch, bd;
    uint32_t sr;
    if (!ctx->audio_started) {
        flag  = TAI_STREAM_START;
        codec = ctx->audio_codec;
        ch    = ctx->audio_channels;
        bd    = ctx->audio_bit_depth;
        sr    = ctx->audio_sample_rate;
        ctx->audio_started = 1;
    } else {
        flag  = TAI_STREAM_MIDDLE;
        codec = 0; ch = 0; bd = 0; sr = 0;
    }

    int hdr_len = tai_proto_build_audio_hdr(ctx,
                                            TAI_DATA_ID_AUDIO_UP, flag,
                                            codec, ch, bd, sr, len,
                                            ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                            sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    int rc = (hdr_len > 0) ? send_app_sg(ctx, (size_t)hdr_len, pcm, len)
                           : hdr_len;
    ctx_unlock(ctx);
    return rc;
}

static int send_audio_end_locked(tai_ctx_t *ctx)
{
    int app_len, hdr_len, rc;

    /* Audio END frame (scatter-gather: header only, no payload) */
    hdr_len = tai_proto_build_audio_hdr(ctx,
                                        TAI_DATA_ID_AUDIO_UP, TAI_STREAM_END,
                                        ctx->audio_codec, ctx->audio_channels,
                                        ctx->audio_bit_depth, ctx->audio_sample_rate,
                                        0,
                                        ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                        sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    if (hdr_len < 0) return hdr_len;
    rc = send_app_sg(ctx, (size_t)hdr_len, NULL, 0);
    if (rc != TAI_OK) return rc;

    /* EventPayloadsEnd (control) */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_AUDIO_UP,
                                                  ctx->tx_ctrl_buf,
                                                  sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;

    /* EventEnd (control) */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_ctrl_buf,
                                         sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    ctx->event_open = 0;
    return rc;
}

int tai_send_audio_end(tai_ctx_t *ctx)
{
    /* Mid-stream continuation: fail-fast on a down link (see tai_send_audio_chunk). */
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "audio_end");
    ctx_lock(ctx);
    int rc = send_audio_end_locked(ctx);
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_image
 * ========================================================================= */
static int send_image_locked(tai_ctx_t *ctx,
                             const uint8_t *data, size_t len,
                             uint8_t format, uint16_t width, uint16_t height)
{
    int app_len, hdr_len, rc;

    /* EventStart (control) */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_ctrl_buf,
                                           sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;
    ctx->event_open = 1;

    /* Image OneShot (scatter-gather: small header + zero-copy image payload) */
    hdr_len = tai_proto_build_image_hdr(ctx, TAI_DATA_ID_IMAGE_UP,
                                        TAI_STREAM_ONE_SHOT, format, width, height,
                                        ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                        sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    if (hdr_len < 0) return hdr_len;
    rc = send_app_sg(ctx, (size_t)hdr_len, data, len);
    if (rc != TAI_OK) return rc;

    /* EventPayloadsEnd (control, data_id = image data id) */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_IMAGE_UP,
                                                  ctx->tx_ctrl_buf,
                                                  sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;

    /* EventEnd (control) */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_ctrl_buf,
                                         sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    ctx->event_open = 0;
    return rc;
}

int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height)
{
    if (!ctx || !data) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    TAI_LOGI(ctx->pal, TAG, "send_image: %zu bytes fmt=%u %ux%u",
             len, format, width, height);
    ctx_lock(ctx);
    int rc = send_image_locked(ctx, data, len, format, width, height);
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_image_with_text
 *
 * Text prompt + image in one event:
 *   EventStart → Text(OneShot) → Image(OneShot) → EventPayloadsEnd → EventEnd
 * ========================================================================= */
static int send_image_with_text_locked(tai_ctx_t *ctx,
                                       const char *text, size_t text_len,
                                       const uint8_t *img_data, size_t img_len,
                                       uint8_t format,
                                       uint16_t width, uint16_t height)
{
    int app_len, hdr_len, rc;

    /* EventStart (control) */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_ctrl_buf,
                                           sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;
    ctx->event_open = 1;

    /* Text OneShot prompt (scatter-gather) */
    hdr_len = tai_proto_build_text_hdr(ctx, TAI_DATA_ID_TEXT_UP,
                                       TAI_STREAM_ONE_SHOT, 0,
                                       ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                       sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    if (hdr_len < 0) return hdr_len;
    rc = send_app_sg(ctx, (size_t)hdr_len, (const uint8_t *)text, text_len);
    if (rc != TAI_OK) return rc;

    /* Image OneShot (scatter-gather) */
    hdr_len = tai_proto_build_image_hdr(ctx, TAI_DATA_ID_IMAGE_UP,
                                        TAI_STREAM_ONE_SHOT, format, width, height,
                                        ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                        sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    if (hdr_len < 0) return hdr_len;
    rc = send_app_sg(ctx, (size_t)hdr_len, img_data, img_len);
    if (rc != TAI_OK) return rc;

    /* EventPayloadsEnd (control) */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_IMAGE_UP,
                                                  ctx->tx_ctrl_buf,
                                                  sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    if (rc != TAI_OK) return rc;

    /* EventEnd (control) */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_ctrl_buf,
                                         sizeof(ctx->tx_ctrl_buf));
    if (app_len < 0) return app_len;
    rc = send_app(ctx, ctx->tx_ctrl_buf, (size_t)app_len);
    ctx->event_open = 0;
    return rc;
}

int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height)
{
    if (!ctx || !text || !img_data) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    TAI_LOGI(ctx->pal, TAG, "send_image_with_text: text=%zu img=%zu bytes",
             text_len, img_len);
    ctx_lock(ctx);
    int rc = send_image_with_text_locked(ctx, text, text_len,
                                         img_data, img_len, format, width, height);
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_chat_break
 * ========================================================================= */
int tai_chat_break(tai_ctx_t *ctx)
{
    if (!ctx) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    ctx_lock(ctx);
    int len = tai_proto_build_event_chat_break(ctx, ctx->tx_ctrl_buf,
                                                sizeof(ctx->tx_ctrl_buf));
    int rc = (len > 0) ? send_app(ctx, ctx->tx_ctrl_buf, (size_t)len) : len;
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_mcp_response
 * ========================================================================= */
int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response)
{
    if (!ctx || !json_rpc_response) return TAI_ERR_ARGS;
    int cc = ensure_connected(ctx);
    if (cc != TAI_OK) return cc;
    ctx_lock(ctx);
    /* MCP response (scatter-gather): header [session][event][event_type] +
     * zero-copy JSON-RPC payload — the control buffer need not hold the JSON. */
    int hdr_len = tai_proto_build_mcp_hdr(ctx,
                                          ctx->tx_hdr_buf + TAI_FRAME_HDR_LEN,
                                          sizeof(ctx->tx_hdr_buf) - TAI_FRAME_HDR_LEN);
    int rc = (hdr_len > 0)
           ? send_app_sg(ctx, (size_t)hdr_len,
                         (const uint8_t *)json_rpc_response, strlen(json_rpc_response))
           : hdr_len;
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * Background worker thread
 *
 * Drives recv + dispatch in a loop, sends periodic pings, detects pong
 * timeouts.  Auto-started by tai_connect(), stopped by tai_disconnect().
 *
 * Locking: the mbedTLS read/write mutex now lives inside the shared TLS module
 * (tls_write / tls_read, granularity = a single ssl_* call), so the worker no
 * longer needs to wrap recv+dispatch in ctx_lock.  rx_buf / frag_buf are touched
 * only from this thread, and dispatch state mutated by callbacks
 * (event_open, session_open, connected) is single-int / advisory.
 * ========================================================================= */

static void *worker_thread(void *arg)
{
    tai_ctx_t *ctx = (tai_ctx_t *)arg;
    TAI_LOGD(ctx->pal, TAG, "worker: started");

    /* The worker owns the disconnect decision: lower layers RETURN a fatal
     * cause, transport faults are detected here, and we fire on_disconnect once
     * on exit. f_reason==0xFF means a clean stop (tai_disconnect / request). */
    uint8_t  f_reason = 0xFF;
    uint8_t  f_detail = 0;
    uint16_t f_code   = 0;

    while (ctx->running) {
        uint64_t now = ctx->pal->time_ms();

        /* Liveness timeout: any inbound traffic counts as alive, not just
         * PONGs. A busy downstream stream proves the link is up even while a
         * ping is overdue, so we never kill an actively-receiving connection. */
        uint64_t last_alive = (ctx->last_rx_ms > ctx->last_pong_ms)
                                  ? ctx->last_rx_ms : ctx->last_pong_ms;
        if (now - last_alive > ctx->ping_timeout_ms) {
            TAI_LOGW(ctx->pal, TAG, "worker: liveness timeout (%llu ms idle)",
                     (unsigned long long)(now - last_alive));
            f_reason = TAI_DISCONNECT_TRANSPORT;
            f_detail = TAI_TRANSPORT_PING_TIMEOUT;
            break;
        }

        /* Periodic ping */
        if (now - ctx->last_ping_ms >= ctx->ping_interval_ms) {
            TAI_LOGI(ctx->pal, TAG, "worker: sending ping at t=%llums (last_pong=%llums ago)",
                     (unsigned long long)now,
                     (unsigned long long)(now - ctx->last_pong_ms));
            int ping_rc = tai_ping(ctx);
            if (ping_rc != TAI_OK) {
                /* The ping is the worker's own TX health probe. A send failure
                 * here means the uplink is broken (the one fault the RX-side
                 * checks can't see), so tear the connection down — this is the
                 * worker thread, so it may fire on_disconnect directly. */
                TAI_LOGE(ctx->pal, TAG, "worker: ping send FAILED rc=%d — disconnecting", ping_rc);
                f_reason = TAI_DISCONNECT_TRANSPORT;
                f_detail = TAI_TRANSPORT_NET_ERROR;
                break;
            }
            TAI_LOGI(ctx->pal, TAG, "worker: ping sent OK");
            ctx->last_ping_ms = now;
        }

        /* Block until data arrives or the next ping is due. Pong timeout is
         * implicitly bounded because ping always wakes us first.  The TLS /
         * raw-TCP layer underneath handles WANT_READ vs WANT_WRITE polling
         * direction correctly, so we no longer need a separate tcp_poll. */
        uint64_t since_ping = ctx->pal->time_ms() - ctx->last_ping_ms;
        uint32_t wait_ms = (since_ping >= ctx->ping_interval_ms)
                               ? 1
                               : (uint32_t)(ctx->ping_interval_ms - since_ping);
        /* Cap the idle block so tai_disconnect (running=0) is noticed within
         * ~TAI_WORKER_POLL_CAP_MS instead of waiting out a whole ping interval. */
        if (wait_ms > TAI_WORKER_POLL_CAP_MS) wait_ms = TAI_WORKER_POLL_CAP_MS;

        uint64_t drain_start = ctx->pal->time_ms();
        int n = tai_recv_data(ctx, wait_ms);
        while (n > 0 && ctx->running) {
            int fatal = tai_process_rx(ctx);
            if (fatal != TAI_OK) {
                if (fatal & TAI_RX_PEER_CLOSE) {
                    f_reason = TAI_DISCONNECT_CONNECTION_CLOSE;
                    f_code   = (uint16_t)(fatal & 0xFFFF);
                } else {
                    f_reason = TAI_DISCONNECT_PROTOCOL;
                    f_detail = (uint8_t)fatal;
                }
                break;
            }
            /* Bound the greedy drain so periodic ping / liveness / shutdown
             * checks run even under a sustained flood; leftover bytes wait for
             * the next pass. */
            if (ctx->pal->time_ms() - drain_start > TAI_DRAIN_BUDGET_MS)
                break;
            n = tai_recv_data(ctx, 0);   /* drain remainder non-blocking */
        }
        if (f_reason != 0xFF) break;     /* fail-fast / CONNECTION_CLOSE during drain */

        if (n == 0) {
            TAI_LOGW(ctx->pal, TAG, "worker: connection EOF");
            f_reason = TAI_DISCONNECT_TRANSPORT;
            f_detail = TAI_TRANSPORT_EOF;
            break;
        }
        if (n < 0 && n != TAI_ERR_AGAIN) {
            TAI_LOGE(ctx->pal, TAG, "worker: recv error %d", n);
            f_reason = TAI_DISCONNECT_TRANSPORT;
            f_detail = TAI_TRANSPORT_NET_ERROR;
            break;
        }
        /* n > 0: budget yield (running still set) -> loop, keep housekeeping.
         * n == TAI_ERR_AGAIN: wait timed out (ping due) or drain finished.
         * running cleared with no fatal: a clean tai_request_disconnect. */
    }

    /* Single-point disconnect: fire on_disconnect exactly once if the worker
     * decided a fatal cause — a transport fault above, or a fail-fast /
     * CONNECTION_CLOSE returned up from dispatch. A clean tai_disconnect /
     * tai_request_disconnect leaves f_reason==0xFF and fires nothing. */
    if (f_reason != 0xFF) {
        ctx->connected = 0;
        ctx->running   = 0;
        tai_emit_disconnect(ctx, f_reason, f_detail, f_code);
    }

    TAI_LOGD(ctx->pal, TAG, "worker: exiting");
    return NULL;
}
