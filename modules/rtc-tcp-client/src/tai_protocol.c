/*
 * tai_protocol.c — High-level application packet builders and dispatcher.
 *
 * Implements Section 7, 8, 9, 10 of the Tuya AI Foundation 2.1 spec.
 *
 * Every tai_proto_build_*() function:
 *   - Takes a tai_ctx_t * (for session/event IDs, proto_ver, biz params)
 *   - Writes a serialised application packet into buf
 *   - Returns byte count on success or TAI_ERR_*
 *
 * tai_proto_dispatch() decodes a received application packet and invokes
 * the appropriate user callback on the context.
 */

#include <stdio.h>   /* snprintf */
#include "tai_internal.h"

#define TAG "proto"

/* =========================================================================
 * Default JSON strings
 * ========================================================================= */
/* Server expects tts.order.supports as an array of format descriptor
 * objects (not strings).  PCM @16kHz is the universal default. */
static const char TAI_DEFAULT_SESSION_ATTRS[] =
    "{\"deviceMcp\":{\"supportCustomMCP\":true},"
    "\"tts.order.supports\":[{\"format\":\"pcm\","
    "\"sampleRate\":16000,\"bitDepth\":\"16\",\"channels\":1}]}";

static const char TAI_DEFAULT_EVENT_USER_DATA[] =
    "{\"sys.workflow\":\"asr-llm-tts\","
    "\"asr.enableVad\":true,"
    "\"tts.alternate\":true,"
    "\"processing.interrupt\":true}";

/* SessionNew data-ID payload (Section 8.1) */
static const uint8_t TAI_SESSION_PAYLOAD[] = {
    0,8, 0,1, 0,3, 0,5, 0,7, 0,4, 0,2, 0,4
};

/* =========================================================================
 * Build a UserData (attr 111) JSON wrapper.
 *
 * The server reads attr 111 as a JSON object and extracts named sub-objects:
 *   {"sessionAttributes":"<json>"}   — for SessionNew
 *   {"chatAttributes":"<json>"}      — for EventStart
 *
 * The inner value is a JSON-encoded string (quotes escaped).
 * Returns total bytes written (excluding NUL), or 0 on overflow.
 * ========================================================================= */
static int build_userdata_json(char *dst, size_t cap,
                                const char *key, const char *value)
{
    size_t w = 0;
    int n = snprintf(dst, cap, "{\"%s\":\"", key);
    if (n < 0 || (size_t)n >= cap) return 0;
    w = (size_t)n;

    for (const char *p = value; *p && w + 3 < cap; p++) {
        if (*p == '"' || *p == '\\') dst[w++] = '\\';
        dst[w++] = *p;
    }

    n = snprintf(dst + w, cap - w, "\"}");
    if (n < 0 || (size_t)n >= cap - w) return 0;
    w += (size_t)n;
    return (int)w;
}

/* =========================================================================
 * Internal: generate a random hex-prefixed ID into out[out_size]
 * ========================================================================= */
static int gen_id(tai_ctx_t *ctx, const char *prefix,
                  char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t rnd[16];
    if (tai_random_bytes(ctx->pal, rnd, sizeof(rnd)) != 0) {
        /* RNG failed: rnd is uninitialized — never hex-encode stack garbage
         * into a wire id. Fail the build so the connect/event aborts cleanly. */
        if (out_size) out[0] = '\0';
        return TAI_ERR_CRYPTO;
    }

    size_t plen = 0;
    while (prefix[plen] && plen + 1 < out_size) {
        out[plen] = prefix[plen];
        plen++;
    }
    for (int i = 0; i < 16 && plen + 2 < out_size; i++, plen += 2) {
        out[plen]   = hex[(rnd[i] >> 4) & 0xF];
        out[plen+1] = hex[rnd[i] & 0xF];
    }
    out[plen < out_size ? plen : out_size - 1] = '\0';
    return TAI_OK;
}

/* =========================================================================
 * tai_proto_build_client_hello
 *
 * Attributes (v2.1):
 *   client-type      (11) : uint8  — client type
 *   client-id        (12) : string — derived_client_id
 *   security-suit    (10) : bytes  — [sign_level(1)][encrypt_random(32)]
 *   max-fragment-len (15) : uint32 — largest transport fragment payload the
 *                                    client uses/accepts (TAI_MAX_FRAGMENT_PAYLOAD)
 *   ping-interval    (20) : uint32 — keepalive interval (ms)
 *
 * Sent unencrypted (sig_len=0 handled by caller).
 * ========================================================================= */
int tai_proto_build_client_hello(tai_ctx_t *ctx,
                                  uint8_t *buf, size_t buf_size)
{
    /* Build security-suit: 1 byte sign_level + 32 bytes encrypt_random */
    uint8_t security_suit[33];
    security_suit[0] = ctx->sign_level;
    memcpy(security_suit + 1, ctx->encrypt_random, 32);

    uint8_t s_ctype[1], s_ping[4], s_maxfrag[4];
    tai_attr_t attrs[5];
    int na = 0;

    attrs[na++] = tai_attr_u8v(TAI_ATTR_CLIENT_TYPE, s_ctype, ctx->client_type);
    if (ctx->client_id && ctx->client_id[0])
        attrs[na++] = tai_attr_strv(TAI_ATTR_CLIENT_ID, ctx->client_id);
    attrs[na++] = tai_attr_bytesv(TAI_ATTR_SECURITY_SUIT, security_suit, 33);
    attrs[na++] = tai_attr_u32v(TAI_ATTR_MAX_FRAGMENT_LEN, s_maxfrag,
                                TAI_MAX_FRAGMENT_PAYLOAD);
    attrs[na++] = tai_attr_u32v(TAI_ATTR_PING_INTERVAL, s_ping,
                                ctx->ping_interval_ms);

    return tai_packet_encode(ctx->proto_ver, TAI_PKT_CLIENT_HELLO,
                              attrs, na, NULL, 0, buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_session_new
 *
 * Attributes: biz-code, biz-tag, session-id, session-attributes
 * Payload:    TAI_SESSION_PAYLOAD (data-ID negotiation bytes)
 * ========================================================================= */
int tai_proto_build_session_new(tai_ctx_t *ctx,
                                 uint8_t *buf, size_t buf_size)
{
    /* Generate a new session ID */
    if (gen_id(ctx, "vcd-session-", ctx->session_id, sizeof(ctx->session_id)) != TAI_OK)
        return TAI_ERR_CRYPTO;

    const char *sess_attrs = ctx->session_attrs_json
                           ? ctx->session_attrs_json
                           : TAI_DEFAULT_SESSION_ATTRS;

    /* Wrap session attrs inside attr 111 (USER_DATA) as JSON:
     * {"sessionAttributes":"<escaped-json>"} */
    size_t ud_cap = strlen("sessionAttributes") + 2 * strlen(sess_attrs) + 32;
    char *ud_json = (char *)ctx->pal->malloc(ud_cap);
    if (!ud_json) return TAI_ERR_MEM;

    build_userdata_json(ud_json, ud_cap,
                        "sessionAttributes", sess_attrs);

    uint8_t s_biz_code[4], s_biz_tag[8];
    tai_attr_t attrs[5];
    int na = 0;
    attrs[na++] = tai_attr_u32v(TAI_ATTR_BIZ_CODE,  s_biz_code, ctx->biz_code);
    attrs[na++] = tai_attr_u64v(TAI_ATTR_BIZ_TAG,   s_biz_tag,  ctx->biz_tag);
    attrs[na++] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[na++] = tai_attr_strv(TAI_ATTR_USER_DATA,  ud_json);
    if (ctx->agent_token && ctx->agent_token[0])
        attrs[na++] = tai_attr_strv(TAI_ATTR_AGENT_TOKEN, ctx->agent_token);

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_SESSION_NEW,
                                attrs, na,
                                TAI_SESSION_PAYLOAD, sizeof(TAI_SESSION_PAYLOAD),
                                buf, buf_size);
    ctx->pal->free(ud_json);
    return rc;
}

/* =========================================================================
 * tai_proto_build_session_close
 * ========================================================================= */
int tai_proto_build_session_close(tai_ctx_t *ctx,
                                   uint8_t *buf, size_t buf_size)
{
    tai_attr_t attrs[1];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    return tai_packet_encode(ctx->proto_ver, TAI_PKT_SESSION_CLOSE,
                              attrs, 1, NULL, 0, buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_event_start
 *
 * Generates a new event ID.  Attributes: session-id, event-id,
 * ai-chat-user-data.  Payload: [event_type=Start(0)]
 * ========================================================================= */
int tai_proto_build_event_start(tai_ctx_t *ctx,
                                 uint8_t *buf, size_t buf_size)
{
    if (gen_id(ctx, "vcd-event-", ctx->event_id, sizeof(ctx->event_id)) != TAI_OK)
        return TAI_ERR_CRYPTO;
    ctx->event_seq = 0;

    const char *udata = ctx->event_user_data_json
                      ? ctx->event_user_data_json
                      : TAI_DEFAULT_EVENT_USER_DATA;

    /* Wrap chat user data inside attr 111 (USER_DATA) as JSON:
     * {"chatAttributes":"<escaped-json>"} */
    size_t ud_cap = strlen("chatAttributes") + 2 * strlen(udata) + 32;
    char *ud_json = (char *)ctx->pal->malloc(ud_cap);
    if (!ud_json) return TAI_ERR_MEM;

    build_userdata_json(ud_json, ud_cap,
                        "chatAttributes", udata);

    tai_attr_t attrs[3];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   ctx->event_id);
    attrs[2] = tai_attr_strv(TAI_ATTR_USER_DATA,  ud_json);

    uint8_t evt_payload[4];
    int eplen = tai_pack_event(ctx->proto_ver, TAI_EVT_START,
                                NULL, 0, evt_payload, sizeof(evt_payload));
    if (eplen < 0) { ctx->pal->free(ud_json); return eplen; }

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                                attrs, 3,
                                evt_payload, (size_t)eplen,
                                buf, buf_size);
    ctx->pal->free(ud_json);
    return rc;
}

/* =========================================================================
 * tai_proto_build_event_payloads_end
 *
 * Signals that all data for data_id has been sent.
 * Attributes: session-id, event-id, payloads-end-data-id
 * ========================================================================= */
int tai_proto_build_event_payloads_end(tai_ctx_t *ctx, uint16_t data_id,
                                        uint8_t *buf, size_t buf_size)
{
    uint8_t s_did[2];
    tai_attr_t attrs[3];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID,           ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,             ctx->event_id);
    attrs[2] = tai_attr_u16v(TAI_ATTR_PAYLOADS_END_DATA_ID, s_did, data_id);

    uint8_t evt_payload[4];
    int eplen = tai_pack_event(ctx->proto_ver, TAI_EVT_PAYLOADS_END,
                                NULL, 0, evt_payload, sizeof(evt_payload));
    if (eplen < 0) return eplen;

    return tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                              attrs, 3,
                              evt_payload, (size_t)eplen,
                              buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_event_end
 * ========================================================================= */
int tai_proto_build_event_end(tai_ctx_t *ctx,
                               uint8_t *buf, size_t buf_size)
{
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   ctx->event_id);

    uint8_t evt_payload[4];
    int eplen = tai_pack_event(ctx->proto_ver, TAI_EVT_END,
                                NULL, 0, evt_payload, sizeof(evt_payload));
    if (eplen < 0) return eplen;

    return tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                              attrs, 2,
                              evt_payload, (size_t)eplen,
                              buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_event_chat_break
 * ========================================================================= */
int tai_proto_build_event_chat_break(tai_ctx_t *ctx,
                                      uint8_t *buf, size_t buf_size)
{
    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   ctx->event_id);

    uint8_t evt_payload[4];
    int eplen = tai_pack_event(ctx->proto_ver, TAI_EVT_CHAT_BREAK,
                                NULL, 0, evt_payload, sizeof(evt_payload));
    if (eplen < 0) return eplen;

    return tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                              attrs, 2,
                              evt_payload, (size_t)eplen,
                              buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_audio_hdr  (scatter-gather: header only)
 *
 * Writes [pkt byte][attr block (audio-params on START/ONE_SHOT)][8-byte media
 * header] into buf and returns its length. The PCM payload stays in the
 * caller's buffer and is streamed separately by the send path — no copy, no
 * malloc. pcm_len is used only to fill the Opus audio-params frame fields (the
 * PCM bytes themselves are not read), matching the legacy builder.
 * audio-params (v2.1): "<codec> <channels> <bitDepth> <sampleRate>[ 0 <bitrate> <dur> <size>]"
 * ========================================================================= */
int tai_proto_build_audio_hdr(tai_ctx_t *ctx,
                              uint16_t data_id, uint8_t stream_flag,
                              uint8_t codec, uint8_t channels,
                              uint8_t bit_depth, uint32_t sample_rate,
                              size_t pcm_len,
                              uint8_t *buf, size_t buf_size)
{
    char audio_params[64];
    int na = 0;
    tai_attr_t attrs[1];
    if ((stream_flag == TAI_STREAM_START || stream_flag == TAI_STREAM_ONE_SHOT)
        && codec != 0 && sample_rate != 0) {
        if (codec == TAI_AUDIO_OPUS) {
            unsigned frame_dur = 0, frame_sz = 0, bit_rate = 0;
            if (pcm_len > 0) {
                frame_sz  = (unsigned)pcm_len;
                frame_dur = frame_sz * 8000 / 16000;
                if (frame_dur > 0)
                    bit_rate = frame_sz * 8 * 1000 / frame_dur;
            }
            snprintf(audio_params, sizeof(audio_params),
                     "%u %u %u %u 0 %u %u %u",
                     (unsigned)codec, (unsigned)channels,
                     (unsigned)bit_depth, (unsigned)sample_rate,
                     bit_rate, frame_dur, frame_sz);
        } else {
            snprintf(audio_params, sizeof(audio_params),
                     "%u %u %u %u",
                     (unsigned)codec, (unsigned)channels,
                     (unsigned)bit_depth, (unsigned)sample_rate);
        }
        attrs[na++] = tai_attr_strv(TAI_ATTR_AUDIO_PARAMS, audio_params);
    }

    /* pkt byte + attr block (no payload). */
    int pos = tai_packet_encode(ctx->proto_ver, TAI_PKT_AUDIO,
                                na > 0 ? attrs : NULL, na, NULL, 0, buf, buf_size);
    if (pos < 0) return pos;

    /* Append the 8-byte media header — the first bytes of the packet payload. */
    int hl = tai_pack_media_hdr(ctx->proto_ver, data_id, stream_flag,
                                ctx->pal->time_ms(), buf + pos, buf_size - (size_t)pos);
    if (hl < 0) return hl;
    return pos + hl;
}

/* =========================================================================
 * tai_proto_build_text_hdr  (scatter-gather: header only)
 *
 * Writes [pkt byte][data_id:2][flags:1][varint seq] into buf; the UTF-8 text
 * stays in the caller's buffer. Text packets carry no attributes.
 * ========================================================================= */
int tai_proto_build_text_hdr(tai_ctx_t *ctx,
                             uint16_t data_id, uint8_t stream_flag, uint32_t seq,
                             uint8_t *buf, size_t buf_size)
{
    int pos = tai_packet_encode(ctx->proto_ver, TAI_PKT_TEXT,
                                NULL, 0, NULL, 0, buf, buf_size);
    if (pos < 0) return pos;

    int hl = tai_pack_text_hdr(ctx->proto_ver, data_id, stream_flag, seq,
                               buf + pos, buf_size - (size_t)pos);
    if (hl < 0) return hl;
    return pos + hl;
}

/* =========================================================================
 * tai_proto_build_image_hdr  (scatter-gather: header only)
 *
 * Writes [pkt byte][attr block (image-params on START/ONE_SHOT)][8-byte media
 * header] into buf; the image bytes stay in the caller's buffer.
 * ========================================================================= */
int tai_proto_build_image_hdr(tai_ctx_t *ctx,
                              uint16_t data_id, uint8_t stream_flag,
                              uint8_t fmt, uint16_t width, uint16_t height,
                              uint8_t *buf, size_t buf_size)
{
    char image_params[32];
    int na = 0;
    tai_attr_t attrs[1];
    if (stream_flag == TAI_STREAM_START || stream_flag == TAI_STREAM_ONE_SHOT) {
        snprintf(image_params, sizeof(image_params),
                 "%u %u %u %u",
                 (unsigned)TAI_IMG_PAYLOAD_RAW,
                 (unsigned)fmt, (unsigned)width, (unsigned)height);
        attrs[na++] = tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, image_params);
    }

    int pos = tai_packet_encode(ctx->proto_ver, TAI_PKT_IMAGE,
                                na > 0 ? attrs : NULL, na, NULL, 0, buf, buf_size);
    if (pos < 0) return pos;

    int hl = tai_pack_media_hdr(ctx->proto_ver, data_id, stream_flag,
                                ctx->pal->time_ms(), buf + pos, buf_size - (size_t)pos);
    if (hl < 0) return hl;
    return pos + hl;
}

/* =========================================================================
 * tai_proto_build_ping
 * ========================================================================= */
int tai_proto_build_ping(tai_ctx_t *ctx, uint8_t *buf, size_t buf_size)
{
    uint8_t s_ts[8];
    tai_attr_t attrs[1];
    attrs[0] = tai_attr_u64v(TAI_ATTR_CLIENT_TIMESTAMP, s_ts,
                              ctx->pal->time_ms());
    return tai_packet_encode(ctx->proto_ver, TAI_PKT_PING,
                              attrs, 1, NULL, 0, buf, buf_size);
}

/* =========================================================================
 * tai_proto_build_mcp_hdr  (scatter-gather: header only)
 *
 * Writes [pkt byte][session-id attr][event-id attr][event_type:2 = MCPCmd] into
 * buf; the JSON-RPC response stays in the caller's buffer as the event data.
 * A fresh event-id is generated for the response.
 * ========================================================================= */
int tai_proto_build_mcp_hdr(tai_ctx_t *ctx, uint8_t *buf, size_t buf_size)
{
    char resp_event_id[64];
    if (gen_id(ctx, "vcd-event-", resp_event_id, sizeof(resp_event_id)) != TAI_OK)
        return TAI_ERR_CRYPTO;

    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   resp_event_id);

    int pos = tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                                attrs, 2, NULL, 0, buf, buf_size);
    if (pos < 0) return pos;

    /* Event payload begins with the 2-byte event type; the JSON-RPC data
     * follows it as the streamed payload. */
    if ((size_t)pos + 2 > buf_size) return TAI_ERR_MEM;
    tai_w16(buf + pos, TAI_EVT_MCP_CMD);
    return pos + 2;
}

/* =========================================================================
 * tai_proto_dispatch
 *
 * Decode the payload of a received application packet and call the
 * appropriate callback on the context.
 * ========================================================================= */
/* Latch the turn id (attr 61) into ctx->rx_event_id to back msg->event_id.
 * A packet that carries attr 61 updates it; a packet WITHOUT one INHERITS the
 * current value (it is NOT reset to ""). The EVENT case clears it right after
 * firing EVT_END, and tai_disconnect clears it on teardown — so one turn id
 * persists across that turn's audio/text packets and never leaks into the next. */
static void latch_event_id(tai_ctx_t *ctx,
                           const tai_attr_t *attrs, int attr_count)
{
    const tai_attr_t *e = tai_attr_find(attrs, attr_count, TAI_ATTR_EVENT_ID);
    if (e && e->len > 0) {
        size_t n = e->len < sizeof(ctx->rx_event_id) - 1
                 ? e->len : sizeof(ctx->rx_event_id) - 1;
        memcpy(ctx->rx_event_id, e->value, n);
        ctx->rx_event_id[n] = '\0';
    }
    /* else: inherit the latched turn id (do not reset). */
}

/* Shared struct emitters — the single place each callback msg is built. */
static void emit_audio(tai_ctx_t *ctx, const uint8_t *data, size_t len,
                       uint8_t stream_flag, uint16_t data_id, uint64_t ts_ms)
{
    if (!ctx->on_audio) return;
    tai_audio_msg_t m = {0};
    m.data           = data;
    m.len            = len;
    m.codec          = ctx->rx_audio_codec;
    m.sample_rate    = ctx->rx_audio_sample_rate;
    m.frame_duration = ctx->rx_audio_frame_duration;
    m.stream_flag    = stream_flag;
    m.data_id        = data_id;
    m.event_id       = ctx->rx_event_id;
    m.timestamp_ms   = ts_ms;
    ctx->on_audio(ctx, &m, ctx->user_data);
}

static void emit_text(tai_ctx_t *ctx, const char *text, size_t len,
                      uint8_t stream_flag, uint16_t data_id, uint32_t seq)
{
    if (!ctx->on_text) return;
    tai_text_msg_t m = {0};
    m.text        = text;
    m.len         = len;
    m.stream_flag = stream_flag;
    m.data_id     = data_id;
    m.seq         = seq;
    m.event_id    = ctx->rx_event_id;
    ctx->on_text(ctx, &m, ctx->user_data);
}

static void emit_event(tai_ctx_t *ctx, uint16_t event_type,
                       const uint8_t *data, size_t len)
{
    if (!ctx->on_event) return;
    tai_event_msg_t m = {0};
    m.event_type = event_type;
    m.data       = data;
    m.len        = len;
    m.event_id   = ctx->rx_event_id;
    ctx->on_event(ctx, &m, ctx->user_data);
}

static void emit_image(tai_ctx_t *ctx, const uint8_t *data, size_t len,
                       uint8_t format, uint16_t width, uint16_t height,
                       uint8_t stream_flag, uint16_t data_id, uint64_t ts_ms)
{
    if (!ctx->on_image) return;
    tai_image_msg_t m = {0};
    m.data         = data;
    m.len          = len;
    m.format       = format;
    m.width        = width;
    m.height       = height;
    m.stream_flag  = stream_flag;
    m.data_id      = data_id;
    m.event_id     = ctx->rx_event_id;
    m.timestamp_ms = ts_ms;
    ctx->on_image(ctx, &m, ctx->user_data);
}

/* Strict known-event set: an unknown event type is fail-fast (§3.4). */
static int is_known_event(uint16_t t)
{
    switch (t) {
    case TAI_EVT_START:      case TAI_EVT_PAYLOADS_END: case TAI_EVT_END:
    case TAI_EVT_ONE_SHOT:   case TAI_EVT_CHAT_BREAK:   case TAI_EVT_SERVER_VAD:
    case TAI_EVT_MCP_CMD:    case TAI_EVT_SERVER_TIMEOVER:
    case TAI_EVT_UPDATE_CONTEXT:
        return 1;
    default:
        return 0;
    }
}

/* =========================================================================
 * Media delivery (AUDIO / TEXT)
 *
 * These handle a COMPLETE application packet — either a FRAG_NONE frame or one
 * reassembled from transport fragments in frag_buf by tai_process_rx. They
 * parse the media/text header, then deliver the body: AUDIO is split into
 * CBR Opus frames of the negotiated frame_size; TEXT is emitted as-is.
 * ========================================================================= */

/* Parse audio-params (attr 80) ONCE per stream into rx_audio_*. codec is taken
 * from f[0] verbatim, never inferred. Idempotent after the first populated
 * call (guarded on rx_audio_frame_size, matching the legacy single-packet
 * behaviour where MIDDLE packets omit the attribute). */
static void parse_audio_params_once(tai_ctx_t *ctx,
                                    const tai_attr_t *attrs, int attr_count)
{
    if (ctx->rx_audio_frame_size != 0) return;
    const tai_attr_t *ap = tai_attr_find(attrs, attr_count, TAI_ATTR_AUDIO_PARAMS);
    if (!ap || ap->len == 0) return;

    char tmp[128];
    size_t cplen = ap->len < sizeof(tmp) - 1 ? ap->len : sizeof(tmp) - 1;
    memcpy(tmp, ap->value, cplen);
    tmp[cplen] = '\0';

    unsigned f[8] = {0};
    sscanf(tmp, "%u %u %u %u %u %u %u %u",
           &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]);

    ctx->rx_audio_codec = (uint8_t)f[0];
    if (f[3] > 0) ctx->rx_audio_sample_rate    = f[3];
    if (f[6] > 0) ctx->rx_audio_frame_duration = (uint16_t)f[6];
    if (f[7] > 0) {
        ctx->rx_audio_frame_size = (uint16_t)f[7];
    } else if (f[5] > 0 && f[6] > 0) {
        ctx->rx_audio_frame_size = (uint16_t)(f[5] * f[6] / 8000);
    }
}

/* Split an audio body into CBR Opus frames of rx_audio_frame_size, emitting a
 * final short remainder. Whole frames are emitted zero-copy from `body` (valid
 * until the frame is consumed from rx_buf). fs==0 (PCM / unknown) delivers the
 * body whole. */
static void media_audio_body(tai_ctx_t *ctx, const uint8_t *body, size_t body_len,
                             uint8_t stream_flag, uint16_t data_id, uint64_t ts_ms)
{
    if (!ctx->on_audio) return;

    uint16_t fs = ctx->rx_audio_frame_size;
    if (fs == 0) {
        if (body_len > 0)
            emit_audio(ctx, body, body_len, stream_flag, data_id, ts_ms);
        return;
    }

    while (body_len >= fs) {
        emit_audio(ctx, body, fs, stream_flag, data_id, ts_ms);
        body += fs; body_len -= fs;
    }
    if (body_len > 0)
        emit_audio(ctx, body, body_len, stream_flag, data_id, ts_ms);
}

/* AUDIO packet payload: [data_id:2][48-bit stream_flag|ts_ms][opus frames…]. */
static int media_audio(tai_ctx_t *ctx,
                       const tai_attr_t *attrs, int attr_count,
                       const uint8_t *payload, size_t payload_len)
{
    if (payload_len < 8) {
        TAI_LOGW(ctx->pal, TAG, "audio media header truncated (%zu < 8)", payload_len);
        return TAI_PROTO_ERR_MEDIA_HDR;
    }
    uint16_t data_id = 0; uint8_t stream_flag = 0; uint64_t ts_ms = 0;
    tai_unpack_media_hdr(payload, 8, &data_id, &stream_flag, &ts_ms);

    /* A new stream (START / ONE_SHOT) renegotiates audio params: clear the
     * parse-once cache so its own audio-params attr is re-read. Otherwise a
     * second downstream stream would inherit the previous stream's frame_size /
     * codec / sample_rate (MIDDLE/END frames legitimately omit the attr, so the
     * cache must persist within a stream but not across streams). */
    if (stream_flag == TAI_STREAM_START || stream_flag == TAI_STREAM_ONE_SHOT) {
        ctx->rx_audio_frame_size     = 0;
        ctx->rx_audio_codec          = 0;
        ctx->rx_audio_sample_rate    = 0;
        ctx->rx_audio_frame_duration = 0;
    }
    parse_audio_params_once(ctx, attrs, attr_count);

    latch_event_id(ctx, attrs, attr_count);
    media_audio_body(ctx, payload + 8, payload_len - 8, stream_flag, data_id, ts_ms);
    return TAI_OK;
}

/* TEXT packet payload: [data_id:2][flags:1][varint seq][text…]. */
static int media_text(tai_ctx_t *ctx,
                      const tai_attr_t *attrs, int attr_count,
                      const uint8_t *payload, size_t payload_len)
{
    if (payload_len < 3) {
        TAI_LOGW(ctx->pal, TAG, "text media header truncated (%zu < 3)", payload_len);
        return TAI_PROTO_ERR_MEDIA_HDR;
    }
    uint16_t data_id = tai_r16(payload);
    uint8_t  stream_flag = (payload[2] >> 6) & 0x03;
    uint32_t seq = 0;
    size_t   consumed = 0;
    if (tai_varint_decode(payload + 3, payload_len - 3, &seq, &consumed) != TAI_OK) {
        TAI_LOGW(ctx->pal, TAG, "text seq varint truncated");
        return TAI_PROTO_ERR_MEDIA_HDR;
    }
    size_t off = 3 + consumed;

    latch_event_id(ctx, attrs, attr_count);
    if (payload_len > off)
        emit_text(ctx, (const char *)(payload + off), payload_len - off,
                  stream_flag, data_id, seq);
    return TAI_OK;
}

/* IMAGE packet payload: [data_id:2][48-bit stream_flag|ts_ms][image bytes…].
 * A received image is delivered chunk-by-chunk (the caller reassembles by
 * stream_flag). image-params (attr 90) on START/ONE_SHOT carries
 * "payload_type format width height"; format/width/height are 0 otherwise. */
static int media_image(tai_ctx_t *ctx,
                       const tai_attr_t *attrs, int attr_count,
                       const uint8_t *payload, size_t payload_len)
{
    if (payload_len < 8) {
        TAI_LOGW(ctx->pal, TAG, "image media header truncated (%zu < 8)", payload_len);
        return TAI_PROTO_ERR_MEDIA_HDR;
    }
    uint16_t data_id = 0; uint8_t stream_flag = 0; uint64_t ts_ms = 0;
    tai_unpack_media_hdr(payload, 8, &data_id, &stream_flag, &ts_ms);

    uint8_t format = 0; uint16_t width = 0, height = 0;
    const tai_attr_t *ip = tai_attr_find(attrs, attr_count, TAI_ATTR_IMAGE_PARAMS);
    if (ip && ip->len > 0) {
        char tmp[64];
        size_t cplen = ip->len < sizeof(tmp) - 1 ? ip->len : sizeof(tmp) - 1;
        memcpy(tmp, ip->value, cplen);
        tmp[cplen] = '\0';
        unsigned f[4] = {0};
        sscanf(tmp, "%u %u %u %u", &f[0], &f[1], &f[2], &f[3]);
        format = (uint8_t)f[1];   /* f[0]=payload_type, f[1]=format, f[2]=w, f[3]=h */
        width  = (uint16_t)f[2];
        height = (uint16_t)f[3];
    }

    latch_event_id(ctx, attrs, attr_count);
    emit_image(ctx, payload + 8, payload_len - 8, format, width, height,
               stream_flag, data_id, ts_ms);
    return TAI_OK;
}

int tai_proto_dispatch(tai_ctx_t *ctx,
                        uint8_t pkt_type,
                        const tai_attr_t *attrs, int attr_count,
                        const uint8_t *payload, size_t payload_len)
{
    switch (pkt_type) {

    case TAI_PKT_PONG:
        ctx->last_pong_ms = ctx->pal->time_ms();
        TAI_LOGD(ctx->pal, TAG, "PONG received");
        break;

    case TAI_PKT_AUDIO:
        /* Whole media packet (FRAG_NONE, or reassembled from frag_buf): parse
         * the media header, split concatenated CBR Opus by frame_size, emit. */
        return media_audio(ctx, attrs, attr_count, payload, payload_len);

    case TAI_PKT_TEXT:
        return media_text(ctx, attrs, attr_count, payload, payload_len);

    case TAI_PKT_IMAGE:
        return media_image(ctx, attrs, attr_count, payload, payload_len);

    case TAI_PKT_EVENT: {
        uint16_t        evt_type;
        const uint8_t  *evt_data;
        size_t          evt_data_len;
        int rc = tai_unpack_event(ctx->proto_ver, payload, payload_len,
                                   &evt_type, &evt_data, &evt_data_len);
        if (rc != TAI_OK) {
            TAI_LOGW(ctx->pal, TAG, "EVENT unpack failed: %d", rc);
            return TAI_PROTO_ERR_EVENT;
        }
        if (!is_known_event(evt_type)) {
            /* Tolerate unknown event types (forward-compat): the EVENT framing
             * decoded cleanly, so the stream is in sync; a server adding a new
             * event type must not knock existing clients offline. Skip it. */
            TAI_LOGW(ctx->pal, TAG, "ignoring unknown event type %u (forward-compat)",
                     evt_type);
            break;
        }

        if (evt_type == TAI_EVT_END) {
            ctx->event_open = 0;
        }

        latch_event_id(ctx, attrs, attr_count);
        emit_event(ctx, evt_type, evt_data, evt_data_len);
        if (evt_type == TAI_EVT_END)
            ctx->rx_event_id[0] = '\0';   /* turn over: clear after firing END */
        break;
    }

    case TAI_PKT_CONNECTION_CLOSE: {
        uint16_t code = 0;
        const tai_attr_t *a = tai_attr_find(attrs, attr_count,
                                             TAI_ATTR_CONNECTION_CLOSE_CODE);
        if (a) code = tai_attr_u16(a);
        TAI_LOGW(ctx->pal, TAG, "CONNECTION_CLOSE: code=%u", code);
        /* Server-initiated close: return it as the fatal cause (with the close
         * code). The worker fires one on_disconnect(CONNECTION_CLOSE, code). */
        return TAI_RX_PEER_CLOSE | code;
    }

    case TAI_PKT_AUTHENTICATE_RESPONSE: {
        /* Server's authentication result for our ClientHello. This is the
         * confirmed-connect signal: a connection-status-code of 200 means the
         * handshake (ClientHello + SessionNew) was accepted and the session is
         * live -- the server starts pushing session traffic immediately after,
         * and does NOT send a separate SessionNew ack. So tai_connect's wait
         * completes here. connection-status-code is HTTP-style (200 == OK),
         * unlike SESSION_NEW's session-status-code (0 == OK), so map it onto the
         * session_ack convention (0 == OK, >0 == error). */
        const tai_attr_t *st = tai_attr_find(attrs, attr_count,
                                             TAI_ATTR_CONNECTION_STATUS_CODE);
        int code = 200;  /* absent => assume OK */
        if (st) {
            if (st->len == 1)      code = st->value[0];
            else if (st->len >= 2) code = tai_r16(st->value);
        }
        ctx->session_ack = (code == 200) ? 0 : code;
        TAI_LOGD(ctx->pal, TAG, "AuthenticateResponse: status=%d", code);
        break;
    }

    case TAI_PKT_SESSION_NEW: {
        /* Server's SessionNew acknowledgement: record the status for the
         * confirmed-connect wait in tai_connect (attr 44; absent => OK).
         * Some protocol versions ack via AuthenticateResponse above instead. */
        const tai_attr_t *st = tai_attr_find(attrs, attr_count,
                                             TAI_ATTR_SESSION_STATUS_CODE);
        int status = 0;  /* default OK */
        if (st) {
            if (st->len == 1)      status = st->value[0];
            else if (st->len >= 2) status = tai_r16(st->value);
        }
        ctx->session_ack = status;
        TAI_LOGD(ctx->pal, TAG, "SessionNew ack: status=%d", status);
        break;
    }

    case TAI_PKT_SESSION_CLOSE: {
        const tai_attr_t *sid = tai_attr_find(attrs, attr_count,
                                               TAI_ATTR_SESSION_ID);
        const tai_attr_t *err = tai_attr_find(attrs, attr_count,
                                               TAI_ATTR_SESSION_CLOSE_CODE);
        char sid_s[64] = {0};
        if (sid) {
            size_t n = sid->len < sizeof(sid_s) - 1
                     ? sid->len : sizeof(sid_s) - 1;
            memcpy(sid_s, sid->value, n);
        }
        uint16_t code = 0;
        if (err) {
            if (err->len == 1)      code = err->value[0];
            else if (err->len >= 2) code = tai_r16(err->value);
        }
        TAI_LOGW(ctx->pal, TAG,
                 "SESSION_CLOSE from server: session=%s code=%u",
                 sid_s, code);
        ctx->session_open = 0;
        tai_emit_disconnect(ctx, TAI_DISCONNECT_SESSION_CLOSE, 0, code);
        break;
    }

    default:
        /* Tolerate unknown / unexpected packet types (forward-compat): a server
         * that introduces a new downstream packet type must not knock existing
         * clients offline. Inbound IMAGE / VIDEO / FILE land here too. The whole
         * packet was already framed, so the byte stream stays in sync — just
         * skip it and keep the link up. */
        TAI_LOGW(ctx->pal, TAG, "ignoring unknown pkt_type=%u (forward-compat)", pkt_type);
        break;
    }

    (void)attrs; (void)attr_count;
    return TAI_OK;
}
