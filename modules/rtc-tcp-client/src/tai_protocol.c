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
static void gen_id(tai_ctx_t *ctx, const char *prefix,
                   char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t rnd[16];
    tai_random_bytes(rnd, sizeof(rnd));

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
}

/* =========================================================================
 * tai_proto_build_client_hello
 *
 * Attributes (v2.1):
 *   client-type   (11) : uint8  — client type
 *   client-id     (12) : string — derived_client_id
 *   security-suit (10) : bytes  — [sign_level(1)][encrypt_random(32)]
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

    uint8_t s_ctype[1], s_ping[4];
    tai_attr_t attrs[4];
    int na = 0;

    attrs[na++] = tai_attr_u8v(TAI_ATTR_CLIENT_TYPE, s_ctype, ctx->client_type);
    if (ctx->client_id && ctx->client_id[0])
        attrs[na++] = tai_attr_strv(TAI_ATTR_CLIENT_ID, ctx->client_id);
    attrs[na++] = tai_attr_bytesv(TAI_ATTR_SECURITY_SUIT, security_suit, 33);
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
    gen_id(ctx, "vcd-session-", ctx->session_id, sizeof(ctx->session_id));

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
    gen_id(ctx, "vcd-event-", ctx->event_id, sizeof(ctx->event_id));
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
 * tai_proto_build_audio
 *
 * Builds an Audio packet.
 * stream_flag == TAI_STREAM_START: includes audio-params attribute.
 * audio-params (v2.1): "<codec> <channels> <bitDepth> <sampleRate>"
 * ========================================================================= */
int tai_proto_build_audio(tai_ctx_t *ctx,
                           uint16_t data_id, uint8_t stream_flag,
                           uint8_t codec, uint8_t channels,
                           uint8_t bit_depth, uint32_t sample_rate,
                           const uint8_t *pcm, size_t pcm_len,
                           uint8_t *buf, size_t buf_size)
{
    char audio_params[64];

    /* Build media header (8 bytes for v2.1) */
    uint8_t media_hdr[23];
    int hdr_len = tai_pack_media_hdr(ctx->proto_ver, data_id, stream_flag,
                                      ctx->pal->time_ms(),
                                      media_hdr, sizeof(media_hdr));
    if (hdr_len < 0) return hdr_len;

    /* Assemble payload = media_hdr + pcm in a heap-allocated buffer so it
     * never overlaps with the output buf that tai_packet_encode writes. */
    size_t payload_len = (size_t)hdr_len + pcm_len;
    uint8_t *payload_buf = (uint8_t *)ctx->pal->malloc(payload_len);
    if (!payload_buf) return TAI_ERR_MEM;

    memcpy(payload_buf, media_hdr, (size_t)hdr_len);
    if (pcm && pcm_len) memcpy(payload_buf + hdr_len, pcm, pcm_len);

    /* Attributes: audio-params only on START/ONE_SHOT per spec Section 9.1 */
    int na = 0;
    tai_attr_t attrs[1];
    if (stream_flag == TAI_STREAM_START || stream_flag == TAI_STREAM_ONE_SHOT) {
        if (codec != 0 && sample_rate != 0) {
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
    }

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_AUDIO,
                                na > 0 ? attrs : NULL, na,
                                payload_buf, payload_len,
                                buf, buf_size);
    ctx->pal->free(payload_buf);
    return rc;
}

/* =========================================================================
 * tai_proto_build_text
 * ========================================================================= */
int tai_proto_build_text(tai_ctx_t *ctx,
                          uint16_t data_id, uint8_t stream_flag, uint32_t seq,
                          const char *text, size_t text_len,
                          uint8_t *buf, size_t buf_size)
{
    uint8_t text_hdr[16];
    int hdr_len = tai_pack_text_hdr(ctx->proto_ver, data_id, stream_flag,
                                     seq, text_hdr, sizeof(text_hdr));
    if (hdr_len < 0) return hdr_len;

    /* Assemble payload in a heap-allocated buffer (no overlap with buf). */
    size_t payload_len = (size_t)hdr_len + text_len;
    uint8_t *payload_buf = (uint8_t *)ctx->pal->malloc(payload_len);
    if (!payload_buf) return TAI_ERR_MEM;

    memcpy(payload_buf, text_hdr, (size_t)hdr_len);
    if (text && text_len) memcpy(payload_buf + hdr_len, text, text_len);

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_TEXT,
                                NULL, 0,
                                payload_buf, payload_len,
                                buf, buf_size);
    ctx->pal->free(payload_buf);
    return rc;
}

/* =========================================================================
 * tai_proto_build_image
 * ========================================================================= */
int tai_proto_build_image(tai_ctx_t *ctx,
                           uint16_t data_id, uint8_t stream_flag,
                           uint8_t fmt, uint16_t width, uint16_t height,
                           const uint8_t *data, size_t data_len,
                           uint8_t *buf, size_t buf_size)
{
    char image_params[32];
    uint8_t media_hdr[23];
    int hdr_len = tai_pack_media_hdr(ctx->proto_ver, data_id, stream_flag,
                                      ctx->pal->time_ms(),
                                      media_hdr, sizeof(media_hdr));
    if (hdr_len < 0) return hdr_len;

    /* Assemble payload in a heap-allocated buffer (no overlap with buf). */
    size_t payload_len = (size_t)hdr_len + data_len;
    uint8_t *payload_buf = (uint8_t *)ctx->pal->malloc(payload_len);
    if (!payload_buf) return TAI_ERR_MEM;

    memcpy(payload_buf, media_hdr, (size_t)hdr_len);
    if (data && data_len) memcpy(payload_buf + hdr_len, data, data_len);

    int na = 0;
    tai_attr_t attrs[1];
    if (stream_flag == TAI_STREAM_START || stream_flag == TAI_STREAM_ONE_SHOT) {
        snprintf(image_params, sizeof(image_params),
                 "%u %u %u %u",
                 (unsigned)TAI_IMG_PAYLOAD_RAW,
                 (unsigned)fmt, (unsigned)width, (unsigned)height);
        attrs[na++] = tai_attr_strv(TAI_ATTR_IMAGE_PARAMS, image_params);
    }

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_IMAGE,
                                na > 0 ? attrs : NULL, na,
                                payload_buf, payload_len,
                                buf, buf_size);
    ctx->pal->free(payload_buf);
    return rc;
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
 * tai_proto_build_mcp_response
 *
 * Sends an Event(MCPCmd) with the JSON-RPC 2.0 response as event data.
 * A fresh event-id is generated for the response.
 * ========================================================================= */
int tai_proto_build_mcp_response(tai_ctx_t *ctx,
                                  const char *json_rpc,
                                  uint8_t *buf, size_t buf_size)
{
    /* New event ID for the response */
    char resp_event_id[64];
    gen_id(ctx, "vcd-event-", resp_event_id, sizeof(resp_event_id));

    tai_attr_t attrs[2];
    attrs[0] = tai_attr_strv(TAI_ATTR_SESSION_ID, ctx->session_id);
    attrs[1] = tai_attr_strv(TAI_ATTR_EVENT_ID,   resp_event_id);

    size_t json_len = json_rpc ? strlen(json_rpc) : 0;

    uint8_t *evt_buf = (uint8_t *)ctx->pal->malloc(4 + json_len);
    if (!evt_buf) return TAI_ERR_MEM;

    int eplen = tai_pack_event(ctx->proto_ver, TAI_EVT_MCP_CMD,
                                (const uint8_t *)json_rpc, json_len,
                                evt_buf, 4 + json_len);
    if (eplen < 0) { ctx->pal->free(evt_buf); return eplen; }

    int rc = tai_packet_encode(ctx->proto_ver, TAI_PKT_EVENT,
                                attrs, 2,
                                evt_buf, (size_t)eplen,
                                buf, buf_size);
    ctx->pal->free(evt_buf);
    return rc;
}

/* =========================================================================
 * tai_proto_dispatch
 *
 * Decode the payload of a received application packet and call the
 * appropriate callback on the context.
 * ========================================================================= */
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

    case TAI_PKT_AUDIO: {
        /* Strip the 8-byte media header */
        size_t hdr = 8;

        /* Parse audio_params from any audio packet (steam sets it on all) */
        if (ctx->rx_audio_frame_size == 0) {
            const tai_attr_t *ap = tai_attr_find(attrs, attr_count,
                                                  TAI_ATTR_AUDIO_PARAMS);
            if (ap && ap->len > 0) {
                char tmp[128];
                size_t cplen = ap->len < sizeof(tmp) - 1 ? ap->len : sizeof(tmp) - 1;
                memcpy(tmp, ap->value, cplen);
                tmp[cplen] = '\0';

                unsigned f[8] = {0};
                sscanf(tmp, "%u %u %u %u %u %u %u %u",
                       &f[0], &f[1], &f[2], &f[3],
                       &f[4], &f[5], &f[6], &f[7]);

                if (f[3] > 0) ctx->rx_audio_sample_rate    = f[3];
                if (f[6] > 0) ctx->rx_audio_frame_duration = (uint16_t)f[6];
                if (f[7] > 0) {
                    ctx->rx_audio_frame_size = (uint16_t)f[7];
                } else if (f[5] > 0 && f[6] > 0) {
                    ctx->rx_audio_frame_size = (uint16_t)(f[5] * f[6] / 8000);
                }
            }
        }

        if (payload_len > hdr && ctx->on_audio) {
            const uint8_t *audio = payload + hdr;
            size_t audio_len = payload_len - hdr;
            uint16_t pkt_sz = ctx->rx_audio_frame_size;

            if (pkt_sz > 0 && audio_len > (size_t)pkt_sz) {
                /* Split concatenated CBR Opus packets */
                const uint8_t *p = audio;
                size_t remaining = audio_len;
                while (remaining >= (size_t)pkt_sz) {
                    ctx->on_audio(ctx, p, pkt_sz,
                                  ctx->rx_audio_sample_rate,
                                  ctx->rx_audio_frame_duration,
                                  ctx->user_data);
                    p += pkt_sz;
                    remaining -= pkt_sz;
                }
                if (remaining > 0)
                    ctx->on_audio(ctx, p, remaining,
                                  ctx->rx_audio_sample_rate,
                                  ctx->rx_audio_frame_duration,
                                  ctx->user_data);
            } else {
                ctx->on_audio(ctx, audio, audio_len,
                              ctx->rx_audio_sample_rate,
                              ctx->rx_audio_frame_duration,
                              ctx->user_data);
            }
        }
        break;
    }

    case TAI_PKT_TEXT: {
        /* Strip text header: [id:2][flags:1][varint_seq] */
        if (payload_len < 3) break;
        uint8_t flags = payload[2];
        uint8_t stream_flag = (flags >> 6) & 0x03;
        size_t  consumed = 0;
        uint32_t seq;
        int rc = tai_varint_decode(payload + 3, payload_len - 3,
                                    &seq, &consumed);
        if (rc != TAI_OK) break;
        size_t offset = 3 + consumed;
        if (ctx->on_text && payload_len > offset)
            ctx->on_text(ctx,
                         (const char *)(payload + offset),
                         payload_len - offset,
                         stream_flag,
                         ctx->user_data);
        break;
    }

    case TAI_PKT_IMAGE: {
        /* Received image (e.g. a cloud-generated picture). Payload starts with
         * the 8-byte media header ([data_id:2][packed_48:6]); stream_flag is
         * the top 2 bits of byte 2. Deliver each chunk to on_image; the caller
         * accumulates START..END (or a single ONE_SHOT) and decodes the result.
         * Image-params (payload-type/format/w/h) ride on the START attrs. */
        if (payload_len < 8) break;
        uint8_t stream_flag = (payload[2] >> 6) & 0x03;
        const uint8_t *img = payload + 8;
        size_t img_len = payload_len - 8;
        if (ctx->on_image)
            ctx->on_image(ctx, img, img_len, stream_flag, ctx->user_data);
        break;
    }

    case TAI_PKT_EVENT: {
        uint16_t        evt_type;
        const uint8_t  *evt_data;
        size_t          evt_data_len;
        int rc = tai_unpack_event(ctx->proto_ver, payload, payload_len,
                                   &evt_type, &evt_data, &evt_data_len);
        if (rc != TAI_OK) {
            TAI_LOGW(ctx->pal, TAG, "EVENT unpack failed: %d", rc);
            break;
        }

        if (evt_type == TAI_EVT_END) {
            ctx->event_open = 0;
        }

        if (ctx->on_event)
            ctx->on_event(ctx, evt_type, evt_data, evt_data_len,
                          ctx->user_data);
        break;
    }

    case TAI_PKT_CONNECTION_CLOSE: {
        uint16_t code = 0;
        const tai_attr_t *a = tai_attr_find(attrs, attr_count,
                                             TAI_ATTR_CONNECTION_CLOSE_CODE);
        if (a) code = tai_attr_u16(a);
        TAI_LOGW(ctx->pal, TAG, "CONNECTION_CLOSE: code=%u", code);
        ctx->connected = 0;
        if (ctx->on_disconnect)
            ctx->on_disconnect(ctx, code, ctx->user_data);
        break;
    }

    case TAI_PKT_SESSION_NEW:
        /* Server acknowledges — nothing needed beyond receipt */
        break;

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
        if (ctx->on_disconnect)
            ctx->on_disconnect(ctx, code, ctx->user_data);
        break;
    }

    default:
        /* Ignore unknown packet types gracefully */
        TAI_LOGW(ctx->pal, TAG, "unknown pkt_type=%u, ignoring", pkt_type);
        break;
    }

    (void)attrs; (void)attr_count;
    return TAI_OK;
}
