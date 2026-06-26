/*
 * examples/opus_chat/main.c -- Opus audio chat demo (iot-sdk + TAI).
 *
 * Reads a WAV file, encodes the PCM to Opus frames, sends them to the
 * Tuya AI server via ai-tcp-sdk, and writes the received TTS audio to
 * an output PCM file.  tai_connect() auto-starts the background receive
 * thread, so the main thread only sends data and sleeps.
 *
 * Build (from ai-tcp-sdk root, with iot-sdk at ../iot-sdk):
 *   cmake -B build -DTAI_PAL_OPENSSL=ON -DTAI_BUILD_EXAMPLES=ON \
 *         -DTAI_IOT_CHAT=ON -DTAI_OPUS_CHAT=ON
 *   cmake --build build
 *
 * Usage:
 *   ./build/tai_opus_chat <input.wav> [devid] [secret_key] [local_key]
 *
 * If no WAV file is given, falls back to a text greeting.
 * WAV must be mono 16-bit signed (any sample rate; auto-resampled to 16 kHz).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "mbedtls/base64.h"
#include <opus/opus.h>

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_reconnect.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ----------------------------------------------------------- */

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"

/* -- Audio constants ---------------------------------------------------- */

#define TARGET_SAMPLE_RATE  16000
#define CHANNELS            1
#define BIT_DEPTH           16
#define OPUS_FRAME_MS       40
#define OPUS_FRAME_SAMPLES  (TARGET_SAMPLE_RATE * OPUS_FRAME_MS / 1000) /* 960 */
#define PCM_FRAME_BYTES     (OPUS_FRAME_SAMPLES * CHANNELS * (BIT_DEPTH / 8))
#define MAX_OPUS_PACKET     1000
#define OUTPUT_FILE         "output_opus_chat.pcm"
#define MAX_WAIT_MS         60000

/* -- WAV header --------------------------------------------------------- */

typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    const uint8_t *data;
    size_t   data_len;
} wav_info_t;

static int wav_parse(const uint8_t *buf, size_t len, wav_info_t *info)
{
    if (len < 44) return -1;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return -1;

    /* Walk chunks to find "fmt " and "data" */
    size_t pos = 12;
    int got_fmt = 0;
    while (pos + 8 <= len) {
        uint32_t ck_size = (uint32_t)buf[pos+4]
                         | ((uint32_t)buf[pos+5] << 8)
                         | ((uint32_t)buf[pos+6] << 16)
                         | ((uint32_t)buf[pos+7] << 24);
        if (memcmp(buf + pos, "fmt ", 4) == 0 && ck_size >= 16) {
            uint16_t fmt = buf[pos+8] | (buf[pos+9] << 8);
            if (fmt != 1) { /* PCM only */
                fprintf(stderr, "[wav] Not PCM format (%u)\n", fmt);
                return -1;
            }
            info->channels        = buf[pos+10] | (buf[pos+11] << 8);
            info->sample_rate     = (uint32_t)buf[pos+12]
                                  | ((uint32_t)buf[pos+13] << 8)
                                  | ((uint32_t)buf[pos+14] << 16)
                                  | ((uint32_t)buf[pos+15] << 24);
            info->bits_per_sample = buf[pos+22] | (buf[pos+23] << 8);
            got_fmt = 1;
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            info->data     = buf + pos + 8;
            info->data_len = ck_size;
            if (info->data + info->data_len > buf + len)
                info->data_len = len - (size_t)(info->data - buf);
            if (got_fmt) return 0;
        }
        pos += 8 + ck_size;
        if (ck_size & 1) pos++; /* RIFF chunks are word-aligned */
    }
    return got_fmt ? 0 : -1;
}

/* -- Simple resampler (linear interpolation) ---------------------------- */

static int16_t *resample_to_16k(const int16_t *src, size_t src_samples,
                                uint32_t src_rate, size_t *out_samples)
{
    if (src_rate == TARGET_SAMPLE_RATE) {
        *out_samples = src_samples;
        int16_t *copy = (int16_t *)malloc(src_samples * sizeof(int16_t));
        if (copy) memcpy(copy, src, src_samples * sizeof(int16_t));
        return copy;
    }

    double ratio = (double)src_rate / (double)TARGET_SAMPLE_RATE;
    size_t dst_n = (size_t)((double)src_samples / ratio);
    *out_samples = dst_n;

    int16_t *dst = (int16_t *)malloc(dst_n * sizeof(int16_t));
    if (!dst) return NULL;

    for (size_t i = 0; i < dst_n; i++) {
        double pos = (double)i * ratio;
        size_t idx = (size_t)pos;
        double frac = pos - (double)idx;
        if (idx + 1 < src_samples)
            dst[i] = (int16_t)((1.0 - frac) * src[idx] + frac * src[idx + 1]);
        else if (idx < src_samples)
            dst[i] = src[idx];
        else
            dst[i] = 0;
    }
    return dst;
}

/* -- Timing helper ------------------------------------------------------ */

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

/* -- Demo context ------------------------------------------------------- */

typedef struct {
    FILE         *audio_fp;
    volatile int  got_done;
    int           got_first_text;
    int           got_first_audio;
    int64_t       send_start_us;
    int64_t       send_end_us;
    int64_t       first_text_us;
    int64_t       first_audio_us;
    int64_t       audio_end_us;
    demo_reconnect_t reconn;       /* app-side reconnect policy/state */
} demo_ctx_t;

/* -- TAI callbacks ------------------------------------------------------ */

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    if (!dc->got_first_text) {
        dc->first_text_us  = now_us();
        dc->got_first_text = 1;
    }
    fwrite(msg->text, 1, msg->len, stdout);
    fflush(stdout);
    if (msg->stream_flag == TAI_STREAM_END ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT)
        printf("\n");
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    if (!dc->got_first_audio) {
        dc->first_audio_us  = now_us();
        dc->got_first_audio = 1;
        printf("\n[TTS audio stream started: %s %u Hz, %u ms/frame, event=%s]\n",
               (msg->codec == TAI_AUDIO_OPUS) ? "Opus" :
               (msg->codec == TAI_AUDIO_PCM)  ? "PCM"  : "unknown",
               (unsigned)msg->sample_rate, (unsigned)msg->frame_duration,
               (msg->event_id && msg->event_id[0]) ? msg->event_id : "(none)");
    }
    if (dc->audio_fp && msg->data && msg->len > 0)
        fwrite(msg->data, 1, msg->len, dc->audio_fp);
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    if (msg->event_type == TAI_EVT_PAYLOADS_END) {
        dc->audio_end_us = now_us();
    } else if (msg->event_type == TAI_EVT_END) {
        if (dc->audio_end_us == 0)
            dc->audio_end_us = now_us();
        dc->got_done = 1;
    } else if (msg->event_type == TAI_EVT_MCP_CMD) {
        tai_send_mcp_response(ctx,
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}");
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg,
                          void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    fprintf(stderr, "\n[TAI disconnected: reason=%u close_code=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
    /* Runs on the worker thread: only flag — the main loop reconnects. */
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -- Minimal JSON helpers (same as other examples) ---------------------- */

static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return p;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int json_get_long(const char *json, const char *key, long *out)
{
    const char *p = json_find_value(json, key);
    if (!p) return -1;
    if (*p != '-' && (*p < '0' || *p > '9')) return -1;
    *out = strtol(p, NULL, 10);
    return 0;
}

static int json_array_first_string(const char *json, const char *key,
                                   char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static char *json_get_object(const char *json, const char *key)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '{') return NULL;
    int depth = 0;
    const char *start = p, *q = p;
    while (*q) {
        if (*q == '{') depth++;
        else if (*q == '}' && --depth == 0) {
            size_t len = (size_t)(q - start + 1);
            char *obj = (char *)malloc(len + 1);
            if (obj) { memcpy(obj, start, len); obj[len] = '\0'; }
            return obj;
        }
        q++;
    }
    return NULL;
}

/* -- Base64 decode (OpenSSL) -------------------------------------------- */

static char *b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t max_dlen = (elen * 3) / 4 + 4;
    char *out = (char *)malloc(max_dlen + 1);
    if (!out) return NULL;
    size_t dlen = 0;
    if (mbedtls_base64_decode((unsigned char *)out, max_dlen, &dlen,
                               (const unsigned char *)encoded, elen) != 0) {
        free(out);
        return NULL;
    }
    out[dlen] = '\0';
    if (out_len) *out_len = dlen;
    return out;
}

/* -- Parse token -------------------------------------------------------- */

typedef struct {
    char     host[256];
    char     tls_sni[256];
    char     derived_client_id[256];
    char     agent_token[256];
    uint16_t port;
    long     biz_code;
    long     biz_tag;
} tai_conn_params_t;

static int parse_token(const char *raw_token, tai_conn_params_t *p)
{
    memset(p, 0, sizeof(*p));
    char *json = NULL;
    {
        size_t dl = 0;
        char  *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{')
            json = decoded;
        else {
            free(decoded);
            json = strdup(raw_token);
        }
    }
    if (!json) return -1;

    char *conn = json_get_object(json, "connect_conf");
    if (!conn) {
        fprintf(stderr, "[parse_token] 'connect_conf' not found\n");
        free(json);
        return -1;
    }

    json_array_first_string(conn, "hosts",   p->host,    sizeof(p->host));
    if (json_array_first_string(conn, "domains", p->tls_sni, sizeof(p->tls_sni)) != 0)
        strncpy(p->tls_sni, p->host, sizeof(p->tls_sni) - 1);

    long port = 0;
    if (json_get_long(conn, "ecc_tls_port", &port) != 0)
        json_get_long(conn, "tcpport", &port);
    p->port = (port > 0) ? (uint16_t)port : 443;

    json_get_string(conn, "derived_client_id",
                    p->derived_client_id, sizeof(p->derived_client_id));
    free(conn);

    char *sess = json_get_object(json, "session_conf");
    if (sess) {
        json_get_string(sess, "agentToken",
                        p->agent_token, sizeof(p->agent_token));
        char *biz = json_get_object(sess, "bizConfig");
        if (biz) {
            json_get_long(biz, "bizCode", &p->biz_code);
            json_get_long(biz, "bizTag",  &p->biz_tag);
            free(biz);
        }
        free(sess);
    }

    free(json);
    if (p->host[0] == '\0') {
        fprintf(stderr, "[parse_token] Could not extract host\n");
        return -1;
    }
    return 0;
}

/* -- Opus encode a 16 kHz PCM buffer ------------------------------------ */

typedef struct {
    uint8_t *buf;      /* concatenated opus packets */
    size_t  *pkt_offs; /* offset of each packet in buf */
    size_t  *pkt_lens; /* length of each packet */
    size_t   n_pkts;
    size_t   total_bytes;
} opus_stream_t;

static int opus_encode_pcm(const int16_t *pcm16k, size_t n_samples,
                           opus_stream_t *out)
{
    int err;
    OpusEncoder *enc = opus_encoder_create(TARGET_SAMPLE_RATE, CHANNELS,
                                           OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc) {
        fprintf(stderr, "[opus] encoder create failed: %s\n",
                opus_strerror(err));
        return -1;
    }

    /* Match esp-opus-encoder: CBR, DTX off, complexity 0, 16 kbps */
    opus_encoder_ctl(enc, OPUS_SET_VBR(0));
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(16000));
    opus_encoder_ctl(enc, OPUS_SET_DTX(0));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(0));

    /* Estimate max packets */
    size_t max_pkts = n_samples / OPUS_FRAME_SAMPLES + 1;
    out->buf      = (uint8_t *)malloc(max_pkts * MAX_OPUS_PACKET);
    out->pkt_offs = (size_t *)malloc(max_pkts * sizeof(size_t));
    out->pkt_lens = (size_t *)malloc(max_pkts * sizeof(size_t));
    if (!out->buf || !out->pkt_offs || !out->pkt_lens) {
        opus_encoder_destroy(enc);
        return -1;
    }

    size_t off = 0, np = 0, total = 0;
    while (off + OPUS_FRAME_SAMPLES <= n_samples) {
        int nb = opus_encode(enc, pcm16k + off, OPUS_FRAME_SAMPLES,
                             out->buf + total, MAX_OPUS_PACKET);
        if (nb < 0) {
            fprintf(stderr, "[opus] encode error at sample %zu: %s\n",
                    off, opus_strerror(nb));
            break;
        }
        out->pkt_offs[np] = total;
        out->pkt_lens[np] = (size_t)nb;
        total += (size_t)nb;
        np++;
        off += OPUS_FRAME_SAMPLES;
    }

    out->n_pkts      = np;
    out->total_bytes  = total;
    opus_encoder_destroy(enc);
    return 0;
}

static void opus_stream_free(opus_stream_t *s)
{
    free(s->buf);
    free(s->pkt_offs);
    free(s->pkt_lens);
    memset(s, 0, sizeof(*s));
}

/* ==================================================================== */
/* main                                                                  */
/* ==================================================================== */

int main(int argc, char *argv[])
{
    const char *input_wav  = (argc >= 2) ? argv[1] : NULL;
    const char *devid      = (argc >= 3) ? argv[2] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 4) ? argv[3] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 5) ? argv[4] : DEFAULT_LOCAL_KEY;

    /* Empty "" or "-" means "no wav, run in text mode" -- useful when
     * callers need to pass devid/keys via later argv slots without
     * supplying an audio file. */
    if (input_wav && (input_wav[0] == '\0' || strcmp(input_wav, "-") == 0))
        input_wav = NULL;

    printf("=== tai_opus_chat demo (worker-thread mode) ===\n");
    printf("Device ID : %s\n", devid);
    printf("Input WAV : %s\n\n", input_wav ? input_wav : "(text mode)");

    /* ---- 1. Load & process audio ---------------------------------------- */

    opus_stream_t opus = {0};
    int have_audio = 0;

    if (input_wav) {
        /* Read file */
        FILE *fp = fopen(input_wav, "rb");
        if (!fp) {
            fprintf(stderr, "[main] Cannot open: %s\n", input_wav);
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsize <= 0) { fclose(fp); return 1; }

        uint8_t *raw = (uint8_t *)malloc((size_t)fsize);
        if (!raw) { fclose(fp); return 1; }
        fread(raw, 1, (size_t)fsize, fp);
        fclose(fp);

        /* Parse WAV header */
        wav_info_t wi;
        if (wav_parse(raw, (size_t)fsize, &wi) != 0) {
            fprintf(stderr, "[main] Invalid WAV file\n");
            free(raw);
            return 1;
        }
        printf("[wav] %u Hz, %u ch, %u bit, %zu data bytes\n",
               wi.sample_rate, wi.channels, wi.bits_per_sample, wi.data_len);

        if (wi.bits_per_sample != 16 || wi.channels != 1) {
            fprintf(stderr, "[main] Only mono 16-bit WAV is supported\n");
            free(raw);
            return 1;
        }

        size_t src_samples = wi.data_len / (wi.channels * (wi.bits_per_sample / 8));

        /* Resample to 16 kHz */
        size_t  pcm16k_samples;
        int16_t *pcm16k = resample_to_16k((const int16_t *)wi.data,
                                           src_samples,
                                           wi.sample_rate,
                                           &pcm16k_samples);
        free(raw);
        if (!pcm16k) { fprintf(stderr, "[main] Resample OOM\n"); return 1; }

        printf("[resample] %zu samples @ %u Hz -> %zu samples @ %d Hz\n",
               src_samples, wi.sample_rate, pcm16k_samples, TARGET_SAMPLE_RATE);

        /* Opus encode */
        if (opus_encode_pcm(pcm16k, pcm16k_samples, &opus) != 0) {
            free(pcm16k);
            return 1;
        }
        free(pcm16k);

        printf("[opus] Encoded %zu frames, %zu total bytes "
               "(%.1f:1 compression)\n",
               opus.n_pkts, opus.total_bytes,
               (double)(pcm16k_samples * 2) / (double)(opus.total_bytes ? opus.total_bytes : 1));
        printf("[opus] Audio duration: %.2f s\n\n",
               (double)pcm16k_samples / TARGET_SAMPLE_RATE);
        have_audio = 1;
    }

    /* ---- 2. IoT SDK init ------------------------------------------------ */

    iot_init_default();

    iot_client_config_t iot_cfg = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = AY,
        .env              = PROD,
        .mqtt_disable_tls = false,
        .message_callback = NULL,
    };
    memcpy((char *)iot_cfg.devid,      devid,      strlen(devid));
    memcpy((char *)iot_cfg.secret_key, secret_key, strlen(secret_key));
    memcpy((char *)iot_cfg.local_key,  local_key,  strlen(local_key));

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (!iot) {
        fprintf(stderr, "[main] iot_client_init failed\n");
        opus_stream_free(&opus);
        return 1;
    }

    /* ---- 3. Fetch session token ----------------------------------------- */

    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); opus_stream_free(&opus); return 1; }

    int ret = iot_client_get_session_token(iot, NULL, token, 4096);
    if (ret != 0 || token[0] == '\0') {
        fprintf(stderr, "[main] iot_client_get_session_token failed: %d\n", ret);
        free(token); iot_client_deinit(iot); opus_stream_free(&opus);
        return 1;
    }
    printf("[main] Session token acquired\n");

    /* ---- 4. Parse token ------------------------------------------------- */

    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        free(token); iot_client_deinit(iot); opus_stream_free(&opus);
        return 1;
    }
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    printf("[main] TAI server: %s:%u (SNI: %s)\n", cp.host, cp.port, cp.tls_sni);
    printf("[main] Client ID : %s\n\n", cp.derived_client_id);

    /* ---- 5. Build TAI context ------------------------------------------- */

    demo_ctx_t dc;
    memset(&dc, 0, sizeof(dc));
    dc.audio_fp = fopen(OUTPUT_FILE, "wb");
    if (!dc.audio_fp)
        fprintf(stderr, "[main] Warning: cannot create %s\n", OUTPUT_FILE);

    const pal_t *pal = tai_pal_posix();

    /* Session attributes: tell the server we support Opus TTS output
     * (matches xiaozhi-esp32-cc tuya_protocol.cc) */
    static const char OPUS_SESSION_ATTRS[] =
        "{\"deviceMcp\":{\"supportCustomMCP\":true},"
        "\"tts.order.supports\":[{\"format\":\"opus\","
        "\"sampleRate\":16000,\"bitDepth\":\"16\",\"channels\":1}]}";

    /* Event user data: use JSON boolean true, not string "true"
     * (matches xiaozhi-esp32-cc SendStartListening) */
    static const char OPUS_EVENT_USER_DATA[] =
        "{\"sys.workflow\":\"asr-llm-tts\","
        "\"asr.enableVad\":true,"
        "\"tts.alternate\":true,"
        "\"processing.interrupt\":true}";

    tai_config_t tai_cfg = {
        .host              = cp.host,
        .port              = cp.port,
        .tls_sni           = cp.tls_sni,
        .device_id = cp.derived_client_id,
        .local_key         = local_key,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = (uint32_t)cp.biz_code,
        .biz_tag           = (uint64_t)cp.biz_tag,
        .agent_token       = cp.agent_token,
        .session_attrs_json    = OPUS_SESSION_ATTRS,
        .event_user_data_json  = OPUS_EVENT_USER_DATA,
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
        .user_data         = &dc,
    };

    void *ctx_buf = pal->malloc(tai_ctx_size());
    if (!ctx_buf) {
        fprintf(stderr, "OOM\n");
        if (dc.audio_fp) fclose(dc.audio_fp);
        free(token); iot_client_deinit(iot); opus_stream_free(&opus);
        return 1;
    }

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) {
        fprintf(stderr, "tai_ctx_init failed\n");
        pal->free(ctx_buf);
        if (dc.audio_fp) fclose(dc.audio_fp);
        free(token); iot_client_deinit(iot); opus_stream_free(&opus);
        return 1;
    }

    /* ---- 6-8. Connect, send, await response — with app-driven reconnect --
     *
     * on_disconnect runs on the worker thread and only flags dc.reconn (it must
     * not self-disconnect). This owning thread does tai_disconnect() +
     * tai_connect() with exponential backoff + a circuit breaker — the correct
     * response to the fail-fast model (see demo_reconnect.h). */
    int done = 0;
    while (!done) {
        printf("[main] Connecting to TAI server...\n");
        int crc = tai_connect(ctx);
        if (crc != TAI_OK) {
            fprintf(stderr, "[main] tai_connect failed: %d\n", crc);
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[main] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                goto cleanup;
            }
            uint32_t delay = demo_reconnect_delay_ms(&dc.reconn);
            fprintf(stderr, "[main] retry connect in %u ms (attempt %d)\n",
                    delay, dc.reconn.attempt + 1);
            usleep(delay * 1000);
            dc.reconn.attempt++;
            dc.reconn.need_reconnect = 0;
            continue;
        }
        demo_reconnect_ok(&dc.reconn);
        printf("[main] Connected.\n\n");

        /* ---- 7. Send audio (Opus) or text ------------------------------- */
        dc.send_start_us = now_us();
        int srv;
        if (have_audio) {
            printf("[main] Sending %zu Opus frames (%zu bytes, %d ms each)...\n",
                   opus.n_pkts, opus.total_bytes, OPUS_FRAME_MS);
            srv = tai_send_audio_start(ctx, TAI_AUDIO_OPUS, CHANNELS,
                                       BIT_DEPTH, TARGET_SAMPLE_RATE);
            for (size_t i = 0; i < opus.n_pkts && srv == TAI_OK; i++) {
                srv = tai_send_audio_chunk(ctx, opus.buf + opus.pkt_offs[i],
                                           opus.pkt_lens[i]);
                if (srv == TAI_OK) usleep(OPUS_FRAME_MS * 1000);
            }
            if (srv == TAI_OK) srv = tai_send_audio_end(ctx);
            if (srv == TAI_OK) printf("[main] All Opus audio sent.\n");
        } else {
            const char *greeting =
                "Hello! This is the opus_chat demo running in worker-thread mode.";
            printf("[main] Sending text: \"%s\"\n", greeting);
            srv = tai_send_text(ctx, greeting, strlen(greeting));
        }

        if (srv == TAI_OK) {
            dc.send_end_us = now_us();
            printf("[main] Send complete (%.1f ms)\n",
                   (double)(dc.send_end_us - dc.send_start_us) / 1000.0);

            /* ---- 8. Wait for AI response (or a disconnect) -------------- */
            printf("[main] Waiting for AI response...\nResponse: ");
            fflush(stdout);
            int waited = 0;
            while (!dc.got_done && !dc.reconn.need_reconnect && waited < MAX_WAIT_MS) {
                usleep(100 * 1000);
                waited += 100;
            }
        } else {
            /* An app-thread send failure is reported synchronously here — the
             * SDK does NOT fire on_disconnect for it (only the worker's own
             * ping does). The TX stream may be desynced, so treat it as a
             * transport fault: request a reconnect so the teardown path below
             * rebuilds the link instead of exiting as a benign timeout. */
            fprintf(stderr, "[main] send failed: %d\n", srv);
            demo_reconnect_signal(&dc.reconn, TAI_DISCONNECT_TRANSPORT, 0);
        }

        if (dc.got_done) {
            done = 1;                                  /* got the full response */
        } else if (!dc.reconn.need_reconnect) {
            printf("\n[main] Timed out after %d s\n", MAX_WAIT_MS / 1000);
            done = 1;                                  /* timeout, link still up */
        } else {
            /* Dropped mid-flow: tear down on this (owning) thread, back off,
             * then loop to reconnect and re-send. */
            fprintf(stderr, "\n[main] disconnected (reason=%u code=%u)\n",
                    dc.reconn.reason, dc.reconn.close_code);
            tai_disconnect(ctx);
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[main] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                done = 1;
            } else {
                uint32_t delay = demo_reconnect_delay_ms(&dc.reconn);
                fprintf(stderr, "[main] reconnect in %u ms (attempt %d)\n",
                        delay, dc.reconn.attempt + 1);
                usleep(delay * 1000);
                dc.reconn.attempt++;
                dc.reconn.need_reconnect = 0;
            }
        }
    }

    /* ---- 9. Latency stats ----------------------------------------------- */

    if (dc.audio_fp) { fclose(dc.audio_fp); dc.audio_fp = NULL; }

    printf("\n========== Latency Stats ==========\n");
    if (dc.got_first_text)
        printf("Send start  -> first text:  %.1f ms\n",
               (double)(dc.first_text_us - dc.send_start_us) / 1000.0);
    else
        printf("Send start  -> first text:  (none received)\n");

    if (dc.got_first_audio)
        printf("Send end    -> first audio: %.1f ms\n",
               (double)(dc.first_audio_us - dc.send_end_us) / 1000.0);
    else
        printf("Send end    -> first audio: (none received)\n");

    if (dc.got_first_audio && dc.audio_end_us)
        printf("First audio -> audio done:  %.1f ms\n",
               (double)(dc.audio_end_us - dc.first_audio_us) / 1000.0);
    printf("====================================\n");

    if (dc.got_done)
        printf("[main] TTS audio saved to: %s\n", OUTPUT_FILE);

cleanup:
    /* ---- 10. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    if (dc.audio_fp) fclose(dc.audio_fp);
    opus_stream_free(&opus);
    free(token);
    iot_client_deinit(iot);

    printf("\nDone.\n");
    return dc.got_done ? 0 : 1;
}
