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
#include <stdio.h>
#include <unistd.h>

#define TAG "client"

/* =========================================================================
 * Internal mutex helpers — pal validity is enforced at tai_ctx_init time.
 * ========================================================================= */
static inline void ctx_lock(tai_ctx_t *ctx)   { ctx->pal->mutex_lock(ctx->mutex); }
static inline void ctx_unlock(tai_ctx_t *ctx) { ctx->pal->mutex_unlock(ctx->mutex); }

/* Unified I/O helpers: dispatch to TLS or raw TCP based on disable_tls */
static inline int ctx_io_send(tai_ctx_t *ctx, const uint8_t *buf, size_t len)
{
    if (!ctx->disable_tls) {
        int r = tls_write(ctx->tls, buf, len, 3000);
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

/* =========================================================================
 * Internal: send_fn adapter for tai_frame_fragment
 * ========================================================================= */
static int tls_send_fn(const uint8_t *buf, size_t len, void *arg)
{
    tai_ctx_t *ctx = (tai_ctx_t *)arg;
    return ctx_io_send(ctx, buf, len);
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
        tai_log_packet(ctx->pal, ctx->proto_ver, 1,
                       pkt_type, attrs, attr_count, payload, payload_len);
    }
}

/* =========================================================================
 * Internal: obtain a send buffer of at least `need` bytes.
 *
 * Returns ctx->tx_app_buf when it is large enough, otherwise heap-allocates
 * via the PAL.  Sets *heap_out=1 when the caller must free the result.
 * ========================================================================= */
static uint8_t *get_send_buf(tai_ctx_t *ctx, size_t need, int *heap_out)
{
    *heap_out = 0;
    if (need <= sizeof(ctx->tx_app_buf))
        return ctx->tx_app_buf;
    uint8_t *buf = (uint8_t *)ctx->pal->malloc(need);
    if (buf) *heap_out = 1;
    return buf;
}

static void put_send_buf(tai_ctx_t *ctx, uint8_t *buf, int heap)
{
    if (heap) ctx->pal->free(buf);
}

/* =========================================================================
 * Internal: encode + send a complete application packet.
 * Handles fragmentation automatically.
 * Caller must hold ctx_lock().
 *
 * If tls_send returns WANT_WRITE (ERR_MEM in the TCP stack), the PAL waits
 * briefly and retries while this caller continues holding ctx_lock(). This
 * preserves the mbedTLS requirement that ssl_read and ssl_write are never
 * entered concurrently on the same context.
 * ========================================================================= */
static int send_app(tai_ctx_t *ctx, const uint8_t *app_bytes, size_t app_len)
{
    if (log_get_level() >= LOG_INFO)
        log_send_packet(ctx, app_bytes, app_len);
    /* If fits in one frame: encode directly into tx_frame_buf */
    if (app_len + ctx->sig_len <= 0xFFFFU) {
        uint16_t seq = tai_next_seq(ctx);
        int frame_len = tai_frame_encode(TAI_FRAG_NONE, seq,
                                          app_bytes, app_len,
                                          ctx->sign_key, ctx->sig_len,
                                          ctx->pal,
                                          ctx->tx_frame_buf,
                                          sizeof(ctx->tx_frame_buf));
        if (frame_len < 0) {
            TAI_LOGE(ctx->pal, TAG, "frame encode failed: %d (app_len=%zu)", frame_len, app_len);
            ctx->seq = (uint16_t)(seq - 1);
            if (ctx->seq == 0) ctx->seq = 0xFFFF;
            return frame_len;
        }
        int rc = ctx_io_send(ctx, ctx->tx_frame_buf,
                                     (size_t)frame_len);
        if (rc != TAI_OK) {
            TAI_LOGE(ctx->pal, TAG, "tls_send failed: %d (frame_len=%d)", rc, frame_len);
            ctx->seq = (uint16_t)(seq - 1);
            if (ctx->seq == 0) ctx->seq = 0xFFFF;
        }
        return rc;
    }

    /* Fragment */
    TAI_LOGD(ctx->pal, TAG, "fragmenting app packet: %zu bytes", app_len);
    return tai_frame_fragment(app_bytes, app_len, &ctx->seq,
                               ctx->sign_key, ctx->sig_len,
                               ctx->pal,
                               ctx->tx_frame_buf, sizeof(ctx->tx_frame_buf),
                               tls_send_fn, ctx);
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
    ctx->on_event              = cfg->on_event;
    ctx->on_disconnect         = cfg->on_disconnect;
    ctx->user_data             = cfg->user_data;

    ctx->ping_interval_ms = cfg->ping_interval_ms ? cfg->ping_interval_ms : 60000U;
    ctx->ping_timeout_ms  = cfg->ping_timeout_ms  ? cfg->ping_timeout_ms  : 90000U;
    ctx->disable_tls      = cfg->disable_tls;

    /* Initialise mutex */
    ctx->mutex = cfg->pal->mutex_create();
    if (!ctx->mutex) return NULL;

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

/* Forward declaration */
static void *worker_thread(void *arg);

int tai_connect(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->pal) return TAI_ERR_ARGS;
    if (!ctx->host || ctx->port == 0) return TAI_ERR_ARGS;

    int rc;

    TAI_LOGI(ctx->pal, TAG, "connecting to %s:%u", ctx->host, ctx->port);

    /* 1. Generate 32-byte random for security-suit */
    rc = tai_random_bytes(ctx->encrypt_random, 32);
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
        ctx->raw_tcp = ctx->pal->tcp_connect(ctx->host, ctx->port);
        if (!ctx->raw_tcp) {
            TAI_LOGE(ctx->pal, TAG, "TCP connect failed to %s:%u", ctx->host, ctx->port);
            return TAI_ERR_NET;
        }
        TAI_LOGI(ctx->pal, TAG, "TCP connected (disable_tls mode)");
    } else {
        tls_config_t tcfg = {
            .host            = ctx->host,
            .port            = ctx->port,
            .sni             = ctx->tls_sni,
            .verify          = TLS_VERIFY_OPTIONAL,  /* non-ESP: optional, matches legacy */
            .use_cert_bundle = true,                 /* ESP-IDF: attach esp_crt_bundle */
            .pal             = ctx->pal,
        };
        ctx->tls = tls_connect(&tcfg);
        if (!ctx->tls) {
            TAI_LOGE(ctx->pal, TAG, "TLS connect failed to %s:%u", ctx->host, ctx->port);
            return TAI_ERR_TLS;
        }
        TAI_LOGI(ctx->pal, TAG, "TLS connected");
    }

    /* 4. ClientHello — sent UNENCRYPTED (sig_len = 0) */
    int app_len = tai_proto_build_client_hello(ctx,
                                                ctx->tx_app_buf,
                                                sizeof(ctx->tx_app_buf));
    if (app_len < 0) {
        TAI_LOGE(ctx->pal, TAG, "build ClientHello failed: %d", app_len);
        tai_disconnect(ctx); return app_len;
    }

    uint16_t seq = tai_next_seq(ctx);
    int frame_len = tai_frame_encode(TAI_FRAG_NONE, seq,
                                      ctx->tx_app_buf, (size_t)app_len,
                                      ctx->sign_key, 0,  /* sig_len=0 */
                                      ctx->pal,
                                      ctx->tx_frame_buf,
                                      sizeof(ctx->tx_frame_buf));
    if (frame_len < 0) { tai_disconnect(ctx); return frame_len; }

    rc = ctx_io_send(ctx, ctx->tx_frame_buf, (size_t)frame_len);
    if (rc != TAI_OK) {
        TAI_LOGE(ctx->pal, TAG, "send ClientHello failed: %d", rc);
        tai_disconnect(ctx); return rc;
    }
    if (log_get_level() >= LOG_INFO)
        log_send_packet(ctx, ctx->tx_app_buf, (size_t)app_len);

    /* 5. SessionNew */
    app_len = tai_proto_build_session_new(ctx,
                                           ctx->tx_app_buf,
                                           sizeof(ctx->tx_app_buf));
    if (app_len < 0) {
        TAI_LOGE(ctx->pal, TAG, "build SessionNew failed: %d", app_len);
        tai_disconnect(ctx); return app_len;
    }

    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) {
        TAI_LOGE(ctx->pal, TAG, "send SessionNew failed: %d", rc);
        tai_disconnect(ctx); return rc;
    }

    ctx->connected    = 1;
    ctx->session_open = 1;

    /* 6. Start background receive thread */
    ctx->last_ping_ms = ctx->pal->time_ms();
    ctx->last_pong_ms = ctx->last_ping_ms;
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
 * tai_disconnect
 * ========================================================================= */
void tai_disconnect(tai_ctx_t *ctx)
{
    if (!ctx) return;

    TAI_LOGI(ctx->pal, TAG, "disconnecting");

    /* Stop background thread first */
    ctx->running = 0;
    if (ctx->thread_handle) {
        ctx->pal->thread_join(ctx->thread_handle);
        ctx->thread_handle = NULL;
        TAI_LOGD(ctx->pal, TAG, "worker thread joined");
    }

    if (ctx->connected && (ctx->tls || ctx->raw_tcp)) {
        ctx_lock(ctx);

        /* SessionClose */
        if (ctx->session_open) {
            int len = tai_proto_build_session_close(ctx,
                                                     ctx->tx_app_buf,
                                                     sizeof(ctx->tx_app_buf));
            if (len > 0)
                send_app(ctx, ctx->tx_app_buf, (size_t)len);
            ctx->session_open = 0;
        }

        ctx_unlock(ctx);
    }

    if (ctx->tls || ctx->raw_tcp) {
        ctx_io_close(ctx);
    }

    ctx->connected = 0;
    ctx->event_open = 0;
    ctx->rx_len     = 0;
    ctx->frag_len   = 0;
    ctx->frag_state = 0;
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
        return rc;
    }

    tai_log_packet(ctx->pal, ctx->proto_ver, 0,
                   pkt_type, attrs, attr_count, payload, payload_len);

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
        ctx->rx_len += (size_t)n;
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
 * is required.  Dispatch may invoke user callbacks; if a callback calls
 * back into tai_send_*(), the recursive ctx mutex will serialise as usual.
 * Returns TAI_OK if at least one packet was processed,
 *         TAI_ERR_AGAIN if no complete frame yet.
 * ========================================================================= */
static int tai_process_rx(tai_ctx_t *ctx)
{
    int processed = 0;

    /* Process all complete frames sitting in rx_buf */
    while (ctx->rx_len >= 5) {
        /* Detect version from first byte */
        int ver = tai_frame_detect_version(ctx->rx_buf[0]);
        if (ver < 0) {
            /* Unknown framing — discard one byte and try again */
            TAI_LOGW(ctx->pal, TAG, "unknown frame byte 0x%02x, discarding", ctx->rx_buf[0]);
            memmove(ctx->rx_buf, ctx->rx_buf + 1, ctx->rx_len - 1);
            ctx->rx_len--;
            continue;
        }

        /* How large is the complete frame? */
        size_t needed = tai_frame_total_size(ctx->rx_buf, ctx->rx_len);
        if (needed == 0 || ctx->rx_len < needed) break;  /* need more data */

        /* Verify HMAC signature */
        int rc = tai_frame_verify(ctx->rx_buf, needed,
                                   ctx->sig_len, ctx->sign_key, ctx->pal);
        if (rc != TAI_OK) {
            /* Signature mismatch: discard this frame */
            TAI_LOGW(ctx->pal, TAG, "HMAC verify failed (frame=%zu bytes), discarding", needed);
            memmove(ctx->rx_buf, ctx->rx_buf + needed, ctx->rx_len - needed);
            ctx->rx_len -= needed;
            continue;
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
        if (rc != TAI_OK) {
            memmove(ctx->rx_buf, ctx->rx_buf + needed, ctx->rx_len - needed);
            ctx->rx_len -= needed;
            continue;
        }

        /* Reassemble fragments */
        int complete = 0;
        const uint8_t *app_bytes = NULL;
        size_t         app_len   = 0;

        if (frag_flag == TAI_FRAG_NONE) {
            app_bytes = payload;
            app_len   = payload_len;
            complete  = 1;
        } else if (frag_flag == TAI_FRAG_FIRST) {
            ctx->frag_len   = 0;
            ctx->frag_state = 1;
            if (payload_len <= sizeof(ctx->frag_buf)) {
                memcpy(ctx->frag_buf, payload, payload_len);
                ctx->frag_len = payload_len;
            } else {
                ctx->frag_state = 0;  /* too large */
            }
        } else if (frag_flag == TAI_FRAG_MIDDLE && ctx->frag_state == 1) {
            if (ctx->frag_len + payload_len <= sizeof(ctx->frag_buf)) {
                memcpy(ctx->frag_buf + ctx->frag_len, payload, payload_len);
                ctx->frag_len += payload_len;
            } else {
                ctx->frag_state = 0;
            }
        } else if (frag_flag == TAI_FRAG_LAST && ctx->frag_state == 1) {
            if (ctx->frag_len + payload_len <= sizeof(ctx->frag_buf)) {
                memcpy(ctx->frag_buf + ctx->frag_len, payload, payload_len);
                ctx->frag_len += payload_len;
                app_bytes = ctx->frag_buf;
                app_len   = ctx->frag_len;
                complete  = 1;
            }
            ctx->frag_state = 0;
        }

        /* Process before consuming: app_bytes for FRAG_NONE points into
         * rx_buf, so we must dispatch before the memmove invalidates it.
         * (FRAG_LAST uses frag_buf, which is unaffected by the memmove.) */
        if (complete && app_bytes && app_len > 0) {
            process_app_packet(ctx, app_bytes, app_len);
            processed++;
        }

        /* Consume frame from rx_buf */
        memmove(ctx->rx_buf, ctx->rx_buf + needed, ctx->rx_len - needed);
        ctx->rx_len -= needed;
    }

    return (processed > 0) ? TAI_OK : TAI_ERR_AGAIN;
}

/* =========================================================================
 * tai_ping (internal — called only by the worker thread)
 * ========================================================================= */
static int tai_ping(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    ctx_lock(ctx);
    int len = tai_proto_build_ping(ctx, ctx->tx_app_buf, sizeof(ctx->tx_app_buf));
    int rc = (len > 0) ? send_app(ctx, ctx->tx_app_buf, (size_t)len) : len;
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_text
 *
 * Complete text query: EventStart → Text(OneShot) → EventPayloadsEnd → EventEnd
 * ========================================================================= */
int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len)
{
    if (!ctx || !ctx->connected || !text) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "send_text: %zu bytes", len);
    ctx_lock(ctx);
    int rc, app_len;

    /* EventStart */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_app_buf,
                                           sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }
    ctx->event_open = 1;

    /* Text OneShot */
    app_len = tai_proto_build_text(ctx,
                                    TAI_DATA_ID_TEXT_UP,
                                    TAI_STREAM_ONE_SHOT, 0,
                                    text, len,
                                    ctx->tx_app_buf, sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventPayloadsEnd */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_TEXT_UP,
                                                  ctx->tx_app_buf,
                                                  sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventEnd */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_app_buf,
                                         sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    ctx->event_open = 0;

    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_audio_start / _chunk / _end
 * ========================================================================= */
int tai_send_audio_start(tai_ctx_t *ctx,
                          uint8_t codec, uint8_t channels,
                          uint8_t bit_depth, uint32_t sample_rate)
{
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "audio_start: codec=%u ch=%u bits=%u rate=%u",
             codec, channels, bit_depth, sample_rate);
    ctx_lock(ctx);

    /* Store audio params — first chunk will send with START flag */
    ctx->audio_codec       = codec;
    ctx->audio_channels    = channels;
    ctx->audio_bit_depth   = bit_depth;
    ctx->audio_sample_rate = sample_rate;
    ctx->audio_started     = 0;

    int app_len = tai_proto_build_event_start(ctx, ctx->tx_app_buf,
                                               sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    int rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc == TAI_OK) ctx->event_open = 1;

    ctx_unlock(ctx);
    return rc;
}

int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len)
{
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

    int app_len = tai_proto_build_audio(ctx,
                                         TAI_DATA_ID_AUDIO_UP,
                                         flag,
                                         codec, ch, bd, sr,
                                         pcm, len,
                                         ctx->tx_app_buf, sizeof(ctx->tx_app_buf));
    int rc = (app_len > 0) ? send_app(ctx, ctx->tx_app_buf, (size_t)app_len)
                           : app_len;
    ctx_unlock(ctx);
    return rc;
}

int tai_send_audio_end(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "audio_end");
    ctx_lock(ctx);

    int app_len, rc;

    /* Audio END frame */
    app_len = tai_proto_build_audio(ctx,
                                     TAI_DATA_ID_AUDIO_UP,
                                     TAI_STREAM_END,
                                     ctx->audio_codec,
                                     ctx->audio_channels,
                                     ctx->audio_bit_depth,
                                     ctx->audio_sample_rate,
                                     NULL, 0,
                                     ctx->tx_app_buf, sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventPayloadsEnd */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_AUDIO_UP,
                                                  ctx->tx_app_buf,
                                                  sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventEnd */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_app_buf,
                                         sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    ctx->event_open = 0;

    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_image
 * ========================================================================= */
int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height)
{
    if (!ctx || !ctx->connected || !data) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "send_image: %zu bytes fmt=%u %ux%u",
             len, format, width, height);
    ctx_lock(ctx);

    int app_len, rc, heap = 0;
    size_t need = 256 + 8 + len;
    uint8_t *app_buf = get_send_buf(ctx, need, &heap);
    size_t app_buf_size = heap ? need : sizeof(ctx->tx_app_buf);
    if (!app_buf) { ctx_unlock(ctx); return TAI_ERR_MEM; }

    /* EventStart */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_app_buf,
                                           sizeof(ctx->tx_app_buf));
    if (app_len < 0) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return rc; }
    ctx->event_open = 1;

    /* Image OneShot */
    app_len = tai_proto_build_image(ctx,
                                     TAI_DATA_ID_IMAGE_UP,
                                     TAI_STREAM_ONE_SHOT,
                                     format, width, height,
                                     data, len,
                                     app_buf, app_buf_size);
    if (app_len < 0) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, app_buf, (size_t)app_len);
    if (rc != TAI_OK) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return rc; }
    put_send_buf(ctx, app_buf, heap);

    /* EventPayloadsEnd (data_id = image data id) */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_IMAGE_UP,
                                                  ctx->tx_app_buf,
                                                  sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventEnd */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_app_buf,
                                         sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    ctx->event_open = 0;

    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_image_with_text
 *
 * Text prompt + image in one event:
 *   EventStart → Text(OneShot) → Image(OneShot) → EventPayloadsEnd → EventEnd
 * ========================================================================= */
int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height)
{
    if (!ctx || !ctx->connected || !text || !img_data) return TAI_ERR_ARGS;
    TAI_LOGI(ctx->pal, TAG, "send_image_with_text: text=%zu img=%zu bytes",
             text_len, img_len);
    ctx_lock(ctx);

    int app_len, rc, heap = 0;
    size_t need = 256 + 8 + img_len;
    uint8_t *app_buf = get_send_buf(ctx, need, &heap);
    size_t app_buf_size = heap ? need : sizeof(ctx->tx_app_buf);
    if (!app_buf) { ctx_unlock(ctx); return TAI_ERR_MEM; }

    /* EventStart */
    app_len = tai_proto_build_event_start(ctx, ctx->tx_app_buf,
                                           sizeof(ctx->tx_app_buf));
    if (app_len < 0) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return rc; }
    ctx->event_open = 1;

    /* Text OneShot (prompt) */
    app_len = tai_proto_build_text(ctx,
                                    TAI_DATA_ID_TEXT_UP,
                                    TAI_STREAM_ONE_SHOT, 0,
                                    text, text_len,
                                    ctx->tx_app_buf, sizeof(ctx->tx_app_buf));
    if (app_len < 0) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return rc; }

    /* Image OneShot */
    app_len = tai_proto_build_image(ctx,
                                     TAI_DATA_ID_IMAGE_UP,
                                     TAI_STREAM_ONE_SHOT,
                                     format, width, height,
                                     img_data, img_len,
                                     app_buf, app_buf_size);
    if (app_len < 0) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, app_buf, (size_t)app_len);
    if (rc != TAI_OK) { put_send_buf(ctx, app_buf, heap); ctx_unlock(ctx); return rc; }
    put_send_buf(ctx, app_buf, heap);

    /* EventPayloadsEnd */
    app_len = tai_proto_build_event_payloads_end(ctx, TAI_DATA_ID_IMAGE_UP,
                                                  ctx->tx_app_buf,
                                                  sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    if (rc != TAI_OK) { ctx_unlock(ctx); return rc; }

    /* EventEnd */
    app_len = tai_proto_build_event_end(ctx, ctx->tx_app_buf,
                                         sizeof(ctx->tx_app_buf));
    if (app_len < 0) { ctx_unlock(ctx); return app_len; }
    rc = send_app(ctx, ctx->tx_app_buf, (size_t)app_len);
    ctx->event_open = 0;

    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_chat_break
 * ========================================================================= */
int tai_chat_break(tai_ctx_t *ctx)
{
    if (!ctx || !ctx->connected) return TAI_ERR_ARGS;
    ctx_lock(ctx);
    int len = tai_proto_build_event_chat_break(ctx, ctx->tx_app_buf,
                                                sizeof(ctx->tx_app_buf));
    int rc = (len > 0) ? send_app(ctx, ctx->tx_app_buf, (size_t)len) : len;
    ctx_unlock(ctx);
    return rc;
}

/* =========================================================================
 * tai_send_mcp_response
 * ========================================================================= */
int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response)
{
    if (!ctx || !ctx->connected || !json_rpc_response) return TAI_ERR_ARGS;
    ctx_lock(ctx);
    int len = tai_proto_build_mcp_response(ctx, json_rpc_response,
                                            ctx->tx_app_buf,
                                            sizeof(ctx->tx_app_buf));
    int rc = (len > 0) ? send_app(ctx, ctx->tx_app_buf, (size_t)len) : len;
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

    int fatal = 0;
    while (ctx->running) {
        uint64_t now = ctx->pal->time_ms();

        /* Pong timeout */
        if (now - ctx->last_pong_ms > ctx->ping_timeout_ms) {
            TAI_LOGW(ctx->pal, TAG, "worker: pong timeout (%llu ms since last pong)",
                     (unsigned long long)(now - ctx->last_pong_ms));
            fatal = 1;
            break;
        }

        /* Periodic ping */
        if (now - ctx->last_ping_ms >= ctx->ping_interval_ms) {
            TAI_LOGI(ctx->pal, TAG, "worker: sending ping at t=%llums (last_pong=%llums ago)",
                     (unsigned long long)now,
                     (unsigned long long)(now - ctx->last_pong_ms));
            int ping_rc = tai_ping(ctx);
            if (ping_rc != TAI_OK) {
                TAI_LOGE(ctx->pal, TAG, "worker: ping send FAILED rc=%d", ping_rc);
            } else {
                TAI_LOGI(ctx->pal, TAG, "worker: ping sent OK");
                ctx->last_ping_ms = now;
            }
        }

        /* Block until data arrives or the next ping is due. Pong timeout is
         * implicitly bounded because ping always wakes us first.  The TLS /
         * raw-TCP layer underneath handles WANT_READ vs WANT_WRITE polling
         * direction correctly, so we no longer need a separate tcp_poll. */
        uint64_t since_ping = ctx->pal->time_ms() - ctx->last_ping_ms;
        uint32_t wait_ms = (since_ping >= ctx->ping_interval_ms)
                               ? 1
                               : (uint32_t)(ctx->ping_interval_ms - since_ping);

        int n = tai_recv_data(ctx, wait_ms);
        while (n > 0) {
            tai_process_rx(ctx);
            n = tai_recv_data(ctx, 0);   /* drain remainder non-blocking */
        }

        if (n == 0) {
            TAI_LOGW(ctx->pal, TAG, "worker: connection EOF");
            fatal = 1;
            break;
        }
        if (n != TAI_ERR_AGAIN) {
            TAI_LOGE(ctx->pal, TAG, "worker: recv error %d", n);
            fatal = 1;
            break;
        }
        /* n == TAI_ERR_AGAIN: either initial wait timed out (ping due) or
         * drain finished — loop back to check pong/ping. */
    }

    if (fatal) {
        ctx->connected = 0;
        ctx->running = 0;
        if (ctx->on_disconnect)
            ctx->on_disconnect(ctx, 0, ctx->user_data);
    }

    TAI_LOGD(ctx->pal, TAG, "worker: exiting");
    return NULL;
}
