/*
 * tai_pkt_log.c -- Structured JSON packet logging.
 *
 * Logs sent and received packets as JSON:
 *   send:          {"packet-type":"...","attributes":{...},"payload":{...}}
 *   receive:       {"packet-type":"...","payload-len":N,"attributes":{...},"payload":{...}}
 *   receive-audio: (audio/video/image -- binary data omitted, metadata only)
 *
 * Log level / volume policy for streaming media (audio/video/image):
 *   - START / END / ONE_SHOT frames  : always logged at INFO
 *   - MIDDLE frames, TAI_LOG_MEDIA_SAMPLE_N > 0 (default):
 *       only every N-th MIDDLE frame is logged, at INFO, with a
 *       "sample-every" / "sample-idx" marker; all others are dropped
 *       (no DEBUG trace) to avoid flooding the log pipeline.
 *   - MIDDLE frames, TAI_LOG_MEDIA_SAMPLE_N == 0:
 *       every MIDDLE frame is logged at DEBUG (developer "flood" mode).
 *
 * Every media packet also carries an "order" field -- a per-direction
 * ordinal that resets on START / ONE_SHOT and increments every frame.
 * This lets operators reconstruct stream progress from the sparse
 * sampled output ("saw order=0,50,100,... then end at order=137").
 *
 * Ping / Pong packets are skipped entirely.
 */

#include <stdio.h>
#include <stdarg.h>
#include "tai_internal.h"

#define TAG "pkt"

/* Sample 1-in-N media MIDDLE frames to INFO.  All non-sampled MIDDLE
 * frames are dropped (no log line).  Set to 0 to disable sampling, in
 * which case every MIDDLE frame logs at DEBUG instead.  Override with
 *     cmake -DTAI_LOG_MEDIA_SAMPLE_N=<n>
 */
#ifndef TAI_LOG_MEDIA_SAMPLE_N
#define TAI_LOG_MEDIA_SAMPLE_N 50
#endif

/* snprintf into buf at *pos; advances pos.  Bails (returning 0) if there is
 * no room left for at least one char + NUL.  Always keeps *pos strictly
 * less than cap so the caller's trailing buf[*pos] = '\0' stays in-bounds. */
static int bput(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
{
    /* Defence in depth: if *pos has somehow advanced past cap (caller bug),
     * cap - *pos would underflow on size_t and feed vsnprintf a gigantic
     * "available" size -- that is the actual overrun.  Check explicitly. */
    if (*pos >= cap || cap - *pos < 2) return 0;

    size_t avail = cap - *pos;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, avail, fmt, ap);
    va_end(ap);

    if (n < 0) {                 /* encoding error: leave pos, NUL-terminate */
        buf[*pos] = '\0';
        return n;
    }
    if ((size_t)n >= avail)      /* output truncated by vsnprintf */
        *pos = cap - 1;
    else
        *pos += (size_t)n;
    return n;
}

/* =========================================================================
 * Base64
 * ========================================================================= */
static size_t b64enc(const uint8_t *src, size_t len, char *dst, size_t cap)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i + 2 < len && o + 4 < cap; i += 3) {
        dst[o++] = T[(src[i] >> 2) & 0x3F];
        dst[o++] = T[((src[i] & 3) << 4) | ((src[i+1] >> 4) & 0xF)];
        dst[o++] = T[((src[i+1] & 0xF) << 2) | ((src[i+2] >> 6) & 3)];
        dst[o++] = T[src[i+2] & 0x3F];
    }
    if (i < len && o + 4 < cap) {
        dst[o++] = T[(src[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            dst[o++] = T[((src[i] & 3) << 4) | ((src[i+1] >> 4) & 0xF)];
            dst[o++] = T[((src[i+1] & 0xF) << 2)];
        } else {
            dst[o++] = T[(src[i] & 3) << 4];
            dst[o++] = '=';
        }
        dst[o++] = '=';
    }
    if (o < cap) dst[o] = '\0';
    return o;
}

/* =========================================================================
 * Name helpers (kebab-case for JSON keys)
 * ========================================================================= */
static const char *pkt_kebab(uint8_t t)
{
    switch (t) {
    case TAI_PKT_CLIENT_HELLO:             return "client-hello";
    case TAI_PKT_PING:                     return "ping";
    case TAI_PKT_PONG:                     return "pong";
    case TAI_PKT_CONNECTION_CLOSE:         return "connection-close";
    case TAI_PKT_SESSION_NEW:              return "session-new";
    case TAI_PKT_SESSION_CLOSE:            return "session-close";
    case TAI_PKT_CONNECTION_REFRESH_REQ:   return "connection-refresh-req";
    case TAI_PKT_CONNECTION_REFRESH_RESP:  return "connection-refresh-resp";
    case TAI_PKT_VIDEO:                    return "video";
    case TAI_PKT_AUDIO:                    return "audio";
    case TAI_PKT_IMAGE:                    return "image";
    case TAI_PKT_FILE:                     return "file";
    case TAI_PKT_TEXT:                     return "text";
    case TAI_PKT_EVENT:                    return "event";
    default:                               return "unknown";
    }
}

static const char *sflag_kebab(uint8_t f)
{
    switch (f) {
    case TAI_STREAM_ONE_SHOT: return "one-shot";
    case TAI_STREAM_START:    return "start";
    case TAI_STREAM_MIDDLE:   return "middle";
    case TAI_STREAM_END:      return "end";
    default:                  return "?";
    }
}

static const char *evt_kebab(uint16_t t)
{
    switch (t) {
    case TAI_EVT_START:            return "start";
    case TAI_EVT_PAYLOADS_END:     return "payloads-end";
    case TAI_EVT_END:              return "end";
    case TAI_EVT_ONE_SHOT:         return "one-shot";
    case TAI_EVT_CHAT_BREAK:       return "chat-break";
    case TAI_EVT_SERVER_VAD:       return "server-vad";
    case TAI_EVT_MCP_CMD:          return "mcp-cmd";
    case TAI_EVT_SERVER_TIMEOVER:  return "server-timeover";
    case TAI_EVT_UPDATE_CONTEXT:   return "update-context";
    default:                       return "?";
    }
}

/* Attribute type -> kebab name (NULL if unknown) */
static const char *aname(uint16_t t)
{
    switch (t) {
    case TAI_ATTR_SECURITY_SUIT:          return "security-suit";
    case TAI_ATTR_CLIENT_TYPE:            return "client-type";
    case TAI_ATTR_CLIENT_ID:              return "client-id";
    case TAI_ATTR_MAX_FRAGMENT_LEN:       return "max-fragment-len";
    case TAI_ATTR_READ_BUFFER_SIZE:       return "read-buffer-size";
    case TAI_ATTR_WRITE_BUFFER_SIZE:      return "write-buffer-size";
    case TAI_ATTR_USERNAME:               return "username";
    case TAI_ATTR_PASSWORD:               return "password";
    case TAI_ATTR_CONNECTION_ID:          return "connection-id";
    case TAI_ATTR_CONNECTION_STATUS_CODE: return "connection-status-code";
    case TAI_ATTR_LATEST_EXPIRE_TS:       return "latest-expire-ts";
    case TAI_ATTR_CONNECTION_CLOSE_CODE:  return "connection-close-code";
    case TAI_ATTR_BIZ_CODE:              return "biz-code";
    case TAI_ATTR_BIZ_TAG:               return "biz-tag";
    case TAI_ATTR_SESSION_ID:            return "session-id";
    case TAI_ATTR_SESSION_STATUS_CODE:   return "session-status-code";
    case TAI_ATTR_AGENT_TOKEN:           return "agent-token";
    case TAI_ATTR_SESSION_CLOSE_CODE:    return "session-close-code";
    case TAI_ATTR_EVENT_ID:              return "event-id";
    case TAI_ATTR_EVENT_TIMESTAMP:       return "event-timestamp";
    case TAI_ATTR_STREAM_START_TS:       return "stream-start-ts";
    case TAI_ATTR_DATA_IDS:              return "data-ids";
    case TAI_ATTR_CMD_DATA:              return "cmd-data";
    case TAI_ATTR_VIDEO_PARAMS:          return "video-params";
    case TAI_ATTR_VIDEO_CODEC_TYPE:      return "video-codec-type";
    case TAI_ATTR_VIDEO_SAMPLE_RATE:     return "video-sample-rate";
    case TAI_ATTR_VIDEO_WIDTH:           return "video-width";
    case TAI_ATTR_VIDEO_HEIGHT:          return "video-height";
    case TAI_ATTR_VIDEO_FPS:             return "video-fps";
    case TAI_ATTR_AUDIO_PARAMS:          return "audio-params";
    case TAI_ATTR_AUDIO_CODEC_TYPE:      return "audio-codec-type";
    case TAI_ATTR_AUDIO_SAMPLE_RATE:     return "audio-sample-rate";
    case TAI_ATTR_AUDIO_CHANNELS:        return "audio-channels";
    case TAI_ATTR_AUDIO_BIT_DEPTH:       return "audio-bit-depth";
    case TAI_ATTR_IMAGE_PARAMS:          return "image-params";
    case TAI_ATTR_IMAGE_FORMAT:          return "image-format";
    case TAI_ATTR_IMAGE_WIDTH:           return "image-width";
    case TAI_ATTR_IMAGE_HEIGHT:          return "image-height";
    case TAI_ATTR_IMAGE_PAYLOAD_TYPE:    return "image-payload-type";
    case TAI_ATTR_FILE_PARAMS:           return "file-params";
    case TAI_ATTR_FILE_FORMAT:           return "file-format";
    case TAI_ATTR_FILE_NAME:             return "file-name";
    case TAI_ATTR_FILE_PAYLOAD_TYPE:     return "file-payload-type";
    case TAI_ATTR_USER_DATA:             return "user-data";
    case TAI_ATTR_CLIENT_TIMESTAMP:      return "client-timestamp";
    case TAI_ATTR_SERVER_TIMESTAMP:      return "server-timestamp";
    case TAI_ATTR_LANGUAGE:              return "language";
    case TAI_ATTR_PAYLOADS_END_DATA_ID:  return "payloads-end-data-id";
    case TAI_ATTR_AI_CHAT_USER_DATA:     return "ai-chat-user-data";
    case TAI_ATTR_SESSION_ATTRIBUTES:    return "session-attributes";
    case TAI_ATTR_SUPPORTED_VIDEOS:      return "supported-videos";
    case TAI_ATTR_ERR_CODE:              return "err-code";
    case TAI_ATTR_ERR_MESSAGE:           return "err-message";
    default:                             return NULL;
    }
}

static int is_int_attr(uint16_t t)
{
    switch (t) {
    case TAI_ATTR_CLIENT_TYPE:
    case TAI_ATTR_MAX_FRAGMENT_LEN:
    case TAI_ATTR_READ_BUFFER_SIZE:
    case TAI_ATTR_WRITE_BUFFER_SIZE:
    case TAI_ATTR_CONNECTION_STATUS_CODE:
    case TAI_ATTR_LATEST_EXPIRE_TS:
    case TAI_ATTR_CONNECTION_CLOSE_CODE:
    case TAI_ATTR_BIZ_CODE:
    case TAI_ATTR_BIZ_TAG:
    case TAI_ATTR_SESSION_STATUS_CODE:
    case TAI_ATTR_SESSION_CLOSE_CODE:
    case TAI_ATTR_EVENT_TIMESTAMP:
    case TAI_ATTR_STREAM_START_TS:
    case TAI_ATTR_CLIENT_TIMESTAMP:
    case TAI_ATTR_SERVER_TIMESTAMP:
    case TAI_ATTR_PAYLOADS_END_DATA_ID:
    case TAI_ATTR_VIDEO_CODEC_TYPE:
    case TAI_ATTR_VIDEO_SAMPLE_RATE:
    case TAI_ATTR_VIDEO_WIDTH:
    case TAI_ATTR_VIDEO_HEIGHT:
    case TAI_ATTR_VIDEO_FPS:
    case TAI_ATTR_AUDIO_CODEC_TYPE:
    case TAI_ATTR_AUDIO_SAMPLE_RATE:
    case TAI_ATTR_AUDIO_CHANNELS:
    case TAI_ATTR_AUDIO_BIT_DEPTH:
    case TAI_ATTR_IMAGE_FORMAT:
    case TAI_ATTR_IMAGE_WIDTH:
    case TAI_ATTR_IMAGE_HEIGHT:
    case TAI_ATTR_IMAGE_PAYLOAD_TYPE:
    case TAI_ATTR_FILE_FORMAT:
    case TAI_ATTR_FILE_PAYLOAD_TYPE:
    case TAI_ATTR_ERR_CODE:
        return 1;
    default:
        return 0;
    }
}

static int is_str_attr(uint16_t t)
{
    switch (t) {
    case TAI_ATTR_CLIENT_ID:
    case TAI_ATTR_USERNAME:
    case TAI_ATTR_PASSWORD:
    case TAI_ATTR_CONNECTION_ID:
    case TAI_ATTR_SESSION_ID:
    case TAI_ATTR_AGENT_TOKEN:
    case TAI_ATTR_EVENT_ID:
    case TAI_ATTR_CMD_DATA:
    case TAI_ATTR_VIDEO_PARAMS:
    case TAI_ATTR_AUDIO_PARAMS:
    case TAI_ATTR_IMAGE_PARAMS:
    case TAI_ATTR_FILE_PARAMS:
    case TAI_ATTR_FILE_NAME:
    case TAI_ATTR_USER_DATA:
    case TAI_ATTR_LANGUAGE:
    case TAI_ATTR_AI_CHAT_USER_DATA:
    case TAI_ATTR_SESSION_ATTRIBUTES:
    case TAI_ATTR_SUPPORTED_VIDEOS:
    case TAI_ATTR_ERR_MESSAGE:
        return 1;
    default:
        return 0;
    }
}

/* =========================================================================
 * Value formatters
 * ========================================================================= */

/* Write a JSON-escaped string (with quotes). Long strings truncated at 1024. */
static void put_jstr(char *buf, size_t cap, size_t *pos,
                     const char *s, size_t slen)
{
    size_t limit = slen > 1024 ? 1024 : slen;
    if (*pos + 1 < cap) buf[(*pos)++] = '"';
    for (size_t i = 0; i < limit && *pos + 4 < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')       { buf[(*pos)++] = '\\'; buf[(*pos)++] = '"'; }
        else if (c == '\\') { buf[(*pos)++] = '\\'; buf[(*pos)++] = '\\'; }
        else if (c == '\n') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'n'; }
        else if (c == '\r') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'r'; }
        else if (c == '\t') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 't'; }
        else if (c >= 0x20) { buf[(*pos)++] = (char)c; }
    }
    if (limit < slen) bput(buf, cap, pos, "...");
    if (*pos + 1 < cap) buf[(*pos)++] = '"';
}

/* Write a base64-encoded value (with quotes). Source truncated at 192 bytes. */
static void put_b64(char *buf, size_t cap, size_t *pos,
                    const uint8_t *data, size_t len)
{
    size_t src_limit = len > 192 ? 192 : len;
    char tmp[260];
    b64enc(data, src_limit, tmp, sizeof(tmp));
    if (len > 192)
        bput(buf, cap, pos, "\"%s...\"", tmp);
    else
        bput(buf, cap, pos, "\"%s\"", tmp);
}

/* Write one attribute value */
static void put_aval(char *buf, size_t cap, size_t *pos,
                     const tai_attr_t *a)
{
    if (is_int_attr(a->type)) {
        if (!a->value || a->len == 0) { bput(buf, cap, pos, "0"); return; }
        switch (a->len) {
        case 1: bput(buf, cap, pos, "%u", (unsigned)a->value[0]); break;
        case 2: bput(buf, cap, pos, "%u", (unsigned)tai_r16(a->value)); break;
        case 4: bput(buf, cap, pos, "%u", (unsigned)tai_r32(a->value)); break;
        case 8: bput(buf, cap, pos, "%llu",
                     (unsigned long long)tai_r64(a->value)); break;
        default: bput(buf, cap, pos, "0"); break;
        }
    } else if (is_str_attr(a->type)) {
        put_jstr(buf, cap, pos, (const char *)a->value, a->len);
    } else {
        put_b64(buf, cap, pos, a->value, a->len);
    }
}

/* Write attributes JSON object */
static void put_attrs(char *buf, size_t cap, size_t *pos,
                      const tai_attr_t *attrs, int count)
{
    bput(buf, cap, pos, "{");
    for (int i = 0; i < count && *pos + 16 < cap; i++) {
        if (i > 0) bput(buf, cap, pos, ",");
        const char *name = aname(attrs[i].type);
        if (name)
            bput(buf, cap, pos, "\"%s\":", name);
        else
            bput(buf, cap, pos, "\"%u\":", (unsigned)attrs[i].type);
        put_aval(buf, cap, pos, &attrs[i]);
    }
    bput(buf, cap, pos, "}");
}

/* =========================================================================
 * Payload formatters (packet-type specific)
 * ========================================================================= */
static void put_payload(char *buf, size_t cap, size_t *pos,
                        uint8_t proto_ver, uint8_t pkt_type,
                        const uint8_t *p, size_t plen)
{
    (void)proto_ver;  /* reserved for future protocol versions */
    if (!p || plen == 0) {
        bput(buf, cap, pos, "null");
        return;
    }

    switch (pkt_type) {

    /* --- Text ----------------------------------------------------------- */
    case TAI_PKT_TEXT: {
        if (plen < 3) { bput(buf, cap, pos, "null"); break; }
        uint16_t data_id = tai_r16(p);
        uint8_t  sf      = (p[2] >> 6) & 0x03;
        uint32_t seq     = 0;
        size_t   offset  = 3;

        {
            size_t consumed = 0;
            if (tai_varint_decode(p + 3, plen - 3, &seq, &consumed) == TAI_OK)
                offset = 3 + consumed;
        }

        size_t text_len = (plen > offset) ? plen - offset : 0;
        bput(buf, cap, pos,
             "{\"data-id\":%u,\"stream-flag\":\"%s\",\"seq\":%u,\"length\":%zu",
             (unsigned)data_id, sflag_kebab(sf), (unsigned)seq, text_len);
        if (text_len > 0) {
            bput(buf, cap, pos, ",\"data\":");
            put_jstr(buf, cap, pos, (const char *)(p + offset), text_len);
        } else {
            bput(buf, cap, pos, ",\"data\":null");
        }
        bput(buf, cap, pos, "}");
        break;
    }

    /* --- Audio / Video / Image ------------------------------------------ */
    case TAI_PKT_AUDIO:
    case TAI_PKT_VIDEO:
    case TAI_PKT_IMAGE: {
        if (plen < 8) { bput(buf, cap, pos, "null"); break; }
        uint16_t data_id = tai_r16(p);
        uint64_t packed  = 0;
        for (int i = 2; i < 8; i++)
            packed = (packed << 8) | p[i];
        uint8_t  sf = (uint8_t)((packed >> 46) & 0x03);
        uint64_t ts = packed & UINT64_C(0x3FFFFFFFFFF);
        bput(buf, cap, pos,
             "{\"id\":%u,\"stream-flag\":\"%s\","
             "\"timestamp\":%llu,\"pts\":0,\"length\":%zu}",
             (unsigned)data_id, sflag_kebab(sf),
             (unsigned long long)ts,
             plen - 8);
        break;
    }

    /* --- Event ---------------------------------------------------------- */
    case TAI_PKT_EVENT: {
        if (plen < 2) { bput(buf, cap, pos, "null"); break; }
        uint16_t evt = tai_r16(p);
        size_t data_off = 2;
        size_t data_len = plen - 2;
        bput(buf, cap, pos, "{\"event-type\":\"%s\"", evt_kebab(evt));
        if (data_len > 0 && data_off + data_len <= plen) {
            bput(buf, cap, pos, ",\"length\":%zu,\"data\":", data_len);
            put_jstr(buf, cap, pos,
                     (const char *)(p + data_off), data_len);
        }
        bput(buf, cap, pos, "}");
        break;
    }

    /* --- SessionNew payload: data-ID negotiation blocks ----------------- */
    case TAI_PKT_SESSION_NEW: {
        size_t off = 0;
        bput(buf, cap, pos, "{\"send-ids\":[");
        if (off + 2 <= plen) {
            uint16_t send_bytes = tai_r16(p + off);
            off += 2;
            size_t end = off + send_bytes;
            for (int first = 1; off + 2 <= end && off + 2 <= plen; off += 2) {
                if (!first) bput(buf, cap, pos, ",");
                bput(buf, cap, pos, "%u", (unsigned)tai_r16(p + off));
                first = 0;
            }
        }
        bput(buf, cap, pos, "],\"recv-ids\":[");
        if (off + 2 <= plen) {
            uint16_t recv_bytes = tai_r16(p + off);
            off += 2;
            size_t end = off + recv_bytes;
            for (int first = 1; off + 2 <= end && off + 2 <= plen; off += 2) {
                if (!first) bput(buf, cap, pos, ",");
                bput(buf, cap, pos, "%u", (unsigned)tai_r16(p + off));
                first = 0;
            }
        }
        bput(buf, cap, pos, "]}");
        break;
    }

    default:
        bput(buf, cap, pos, "null");
        break;
    }
}

/* =========================================================================
 * tai_log_packet
 * ========================================================================= */
/* Heap-allocated formatting buffer; tuned to fit a typical structured log
 * line after string/base64 truncation in put_jstr / put_b64.  Long fields
 * are truncated gracefully when this fills. */
#define TAI_LOG_BUF_SIZE 512

void tai_log_packet(const pal_t *pal, uint8_t proto_ver,
                    int is_send,
                    uint8_t pkt_type,
                    const tai_attr_t *attrs, int attr_count,
                    const uint8_t *payload, size_t payload_len)
{
    /* Skip keepalive noise */
    if (pkt_type == TAI_PKT_PING || pkt_type == TAI_PKT_PONG) return;

    /* --- Decide effective log level / volume for media streams ----------
     *
     * Non-media packets always log at INFO.  Media packets (audio / video
     * / image) have much higher rates and need filtering:
     *
     *   - START / END / ONE_SHOT  -> always INFO (stream boundaries)
     *   - MIDDLE, N > 0           -> log every N-th frame at INFO;
     *                                non-sampled frames are dropped
     *                                entirely (return early)
     *   - MIDDLE, N == 0          -> log every MIDDLE at DEBUG (flood mode)
     *
     * A per-direction "order" counter lets operators stitch sampled
     * output back into a continuous timeline; it resets on START /
     * ONE_SHOT and advances on every subsequent media frame.
     */
    int      log_level  = TAI_LOG_INFO;
    uint32_t sample_idx = 0;          /* non-zero => sampled middle frame */
    int      is_media   = (pkt_type == TAI_PKT_AUDIO ||
                           pkt_type == TAI_PKT_VIDEO ||
                           pkt_type == TAI_PKT_IMAGE);
    uint8_t  stream_flag = 0xFF;      /* 0xFF = not-a-media-packet */
    uint32_t order       = 0;

    if (is_media && payload_len >= 3) {
        /* Media headers place stream-flag in the top 2 bits of payload[2]. */
        stream_flag = (payload[2] >> 6) & 0x03;

        /* Per-direction ordinal (non-atomic; races only shift order by
         * a few, which is fine for a human-readable counter). */
        static uint32_t recv_order = 0, send_order = 0;
        uint32_t *op = is_send ? &send_order : &recv_order;

        if (stream_flag == TAI_STREAM_START ||
            stream_flag == TAI_STREAM_ONE_SHOT) {
            *op = 0;
            order = 0;
        } else {
            *op += 1;
            order = *op;
        }

        if (stream_flag == TAI_STREAM_MIDDLE) {
            if (TAI_LOG_MEDIA_SAMPLE_N > 0) {
                /* Sample every N-th; drop the rest entirely. */
                static uint32_t sample_counter = 0;
                uint32_t n = ++sample_counter;
                if ((n % TAI_LOG_MEDIA_SAMPLE_N) != 0)
                    return;              /* dropped -- no log line */
                sample_idx = n;          /* keep at INFO, mark sampled */
            } else {
                /* Flood mode: developer opt-in.  All MIDDLE at DEBUG. */
                log_level = TAI_LOG_DEBUG;
            }
        }
    }

    /* Runtime filter: bail before doing any expensive formatting. */
    if (log_get_level() < log_level) return;

    const char *dir;
    if (is_send) {
        dir = "send";
    } else {
        switch (pkt_type) {
        case TAI_PKT_AUDIO: dir = "receive-audio"; break;
        case TAI_PKT_VIDEO: dir = "receive-video"; break;
        case TAI_PKT_IMAGE: dir = "receive-image"; break;
        default:            dir = "receive"; break;
        }
    }

    char *buf = (char *)pal->malloc(TAI_LOG_BUF_SIZE);
    if (!buf) return;
    size_t cap = TAI_LOG_BUF_SIZE - 1;
    size_t pos = 0;

    bput(buf, cap, &pos, "{\"packet-type\":\"%s\"", pkt_kebab(pkt_type));

    if (is_media && stream_flag != 0xFF)
        bput(buf, cap, &pos, ",\"order\":%u", (unsigned)order);

    if (sample_idx)
        bput(buf, cap, &pos, ",\"sample-every\":%u,\"sample-idx\":%u",
             (unsigned)TAI_LOG_MEDIA_SAMPLE_N, (unsigned)sample_idx);

    if (!is_send)
        bput(buf, cap, &pos, ",\"payload-len\":%zu", payload_len);

    bput(buf, cap, &pos, ",\"attributes\":");
    put_attrs(buf, cap, &pos, attrs, attr_count);

    bput(buf, cap, &pos, ",\"payload\":");
    put_payload(buf, cap, &pos, proto_ver, pkt_type, payload, payload_len);

    bput(buf, cap, &pos, "}");
    buf[pos] = '\0';

    log_emit(log_level, "[" TAG "] %s:  %s", dir, buf);
    pal->free(buf);
}
