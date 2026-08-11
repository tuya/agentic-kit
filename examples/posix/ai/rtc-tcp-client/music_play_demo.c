/*
 * music_play_demo.c -- Music play demo using the rtc-tcp-client library.
 *
 * Sends a text query that triggers the server's music skill, parses the
 * returned audio metadata (artist / album / song / url), prints it, and
 * downloads the mp3 trial clip via curl.
 *
 * Build:
 *   cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
 *   cmake --build build --target tai_music_play_demo
 *
 * Usage:
 *   ./build/tai_music_play_demo [query] [devid] [secret_key] [local_key]
 *
 * Defaults:
 *   query      = "播放周杰伦的歌"
 *   devid / keys = baked-in test device
 *
 * Requires: curl (for downloading the mp3 clip)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "mbedtls/base64.h"

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_reconnect.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ----------------------------------------------------------- */

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"
#define DEFAULT_QUERY      "播放周杰伦的歌"

#define MAX_WAIT_MS  60000
#define OUTPUT_FILE  "output_music.mp3"

/* -- Demo context ------------------------------------------------------- */

typedef struct {
    volatile int     got_done;
    volatile int     music_state;    /* try_parse_music result: 0 none seen,
                                        1 parsed and printed, -1 unreadable  */
    char             music_url[1024];/* trial-clip URL, if any               */
    demo_reconnect_t reconn;
} demo_ctx_t;

/* -------------------------------------------------------------------------
 * Metadata box — padded by display column, since the metadata is Chinese and
 * a CJK character is 3 bytes but 2 columns wide.
 * ------------------------------------------------------------------------- */

#define BOX_INNER  42                          /* columns between the '|'s   */
#define BOX_LABEL  8                           /* label column width         */
#define BOX_VALUE  (BOX_INNER - BOX_LABEL - 3) /* " " + label + ": "         */

static size_t utf8_seq_len(unsigned char c)
{
    if (c < 0x80)          return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  /* invalid lead byte: consume it alone */
}

/* East Asian Wide / Fullwidth code points occupy two terminal columns. */
static size_t utf8_seq_cols(const unsigned char *p, size_t len)
{
    if (len < 3) return 1;
    uint32_t cp;
    if (len == 3)
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
             (uint32_t)(p[2] & 0x3F);
    else
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6)  | (uint32_t)(p[3] & 0x3F);

    if ((cp >= 0x1100  && cp <= 0x115F)  ||  /* Hangul Jamo                */
        (cp >= 0x2E80  && cp <= 0xA4CF)  ||  /* CJK radicals … Yi          */
        (cp >= 0xAC00  && cp <= 0xD7A3)  ||  /* Hangul syllables           */
        (cp >= 0xF900  && cp <= 0xFAFF)  ||  /* CJK compatibility ideographs */
        (cp >= 0xFE30  && cp <= 0xFE6F)  ||  /* CJK compatibility forms    */
        (cp >= 0xFF00  && cp <= 0xFF60)  ||  /* Fullwidth forms            */
        (cp >= 0xFFE0  && cp <= 0xFFE6)  ||
        (cp >= 0x1F300 && cp <= 0x1F9FF) ||  /* emoji                      */
        (cp >= 0x20000 && cp <= 0x3FFFD))    /* CJK extension B and beyond */
        return 2;
    return 1;
}

/* Bytes of the longest prefix of `s` that fits max_cols display columns;
 * *out_cols receives that prefix's width. Never splits a UTF-8 sequence. */
static size_t utf8_prefix(const char *s, size_t max_cols, size_t *out_cols)
{
    size_t n = 0, cols = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        size_t len = utf8_seq_len(*p);
        for (size_t i = 1; i < len; i++) if (!p[i]) { len = i; break; }
        size_t w = utf8_seq_cols(p, len);
        if (cols + w > max_cols) break;
        n    += len;
        cols += w;
        p    += len;
    }
    *out_cols = cols;
    return n;
}

static size_t utf8_width(const char *s)
{
    size_t cols = 0;
    utf8_prefix(s, (size_t)-1, &cols);
    return cols;
}

static void box_rule(void)
{
    printf("  +");
    for (int i = 0; i < BOX_INNER; i++) putchar('-');
    printf("+\n");
}

static void box_center(const char *title)
{
    size_t w    = utf8_width(title);
    size_t left = (w < BOX_INNER) ? (BOX_INNER - w) / 2 : 0;
    printf("  |");
    for (size_t i = 0; i < left; i++) putchar(' ');
    printf("%s", title);
    for (size_t i = left + w; i < BOX_INNER; i++) putchar(' ');
    printf("|\n");
}

/* Print "| <label>: <value>", truncating the value on a character boundary. */
static void box_field(const char *label, const char *value)
{
    size_t cols = 0;
    size_t n    = utf8_prefix(value, BOX_VALUE, &cols);

    printf("  | %-*s: %.*s", BOX_LABEL, label, (int)n, value);
    for (size_t i = cols; i < BOX_VALUE; i++) putchar(' ');
    printf("|\n");
}

/* -------------------------------------------------------------------------
 * Minimal JSON helpers
 * ------------------------------------------------------------------------- */

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
    if (!p || *p != '\"') return -1;
    p++;
    const char *end = strchr(p, '\"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static char *json_get_object_raw(const char *json, const char *key)
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

static char *json_get_array_raw(const char *json, const char *key)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') return NULL;
    int depth = 0;
    const char *start = p, *q = p;
    while (*q) {
        if (*q == '[') depth++;
        else if (*q == ']' && --depth == 0) {
            size_t len = (size_t)(q - start + 1);
            char *obj = (char *)malloc(len + 1);
            if (obj) { memcpy(obj, start, len); obj[len] = '\0'; }
            return obj;
        }
        q++;
    }
    return NULL;
}

static int json_array_first_string(const char *json, const char *key,
                                   char *out, size_t cap)
{
    const char *p = json_find_value(json, key);
    if (!p || *p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if (*p != '\"') return -1;
    p++;
    const char *end = strchr(p, '\"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* -------------------------------------------------------------------------
 * Base64 + token parsing
 * ------------------------------------------------------------------------- */

static char *b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t dlen = 0;
    if (mbedtls_base64_decode(NULL, 0, &dlen,
                               (const unsigned char *)encoded, elen)
            != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return NULL;
    char *out = (char *)malloc(dlen + 1);
    if (!out) return NULL;
    if (mbedtls_base64_decode((unsigned char *)out, dlen, &dlen,
                               (const unsigned char *)encoded, elen) != 0) {
        free(out);
        return NULL;
    }
    out[dlen] = '\0';
    if (out_len) *out_len = dlen;
    return out;
}

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
        char *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{') {
            json = decoded;
        } else {
            free(decoded);
            json = strdup(raw_token);
        }
    }
    if (!json) return -1;

    char *conn = json_get_object_raw(json, "connect_conf");
    if (!conn) { free(json); return -1; }
    json_array_first_string(conn, "hosts",  p->host, sizeof(p->host));
    if (json_array_first_string(conn, "domains", p->tls_sni, sizeof(p->tls_sni)) != 0)
        strncpy(p->tls_sni, p->host, sizeof(p->tls_sni) - 1);

    const char *pp = json_find_value(conn, "ecc_tls_port");
    long port = 0;
    if (pp) port = strtol(pp, NULL, 10);
    p->port = (port > 0) ? (uint16_t)port : 443;

    json_get_string(conn, "derived_client_id",
                    p->derived_client_id, sizeof(p->derived_client_id));
    free(conn);

    char *sess = json_get_object_raw(json, "session_conf");
    if (sess) {
        json_get_string(sess, "agentToken",
                        p->agent_token, sizeof(p->agent_token));
        char *biz = json_get_object_raw(sess, "bizConfig");
        if (biz) {
            const char *bc = json_find_value(biz, "bizCode");
            const char *bt = json_find_value(biz, "bizTag");
            if (bc) p->biz_code = strtol(bc, NULL, 10);
            if (bt) p->biz_tag  = strtol(bt, NULL, 10);
            free(biz);
        }
        free(sess);
    }
    free(json);

    if (p->host[0] == '\0') return -1;
    return 0;
}

/* -------------------------------------------------------------------------
 * NLG content extraction (for clean streaming output)
 * ------------------------------------------------------------------------- */

static const char *nlg_extract_content(const char *text, size_t len,
                                       size_t *out_len)
{
    (void)len;
    if (!strstr(text, "\"NLG\"") || !strstr(text, "\"content\""))
        return NULL;

    const char *p = strstr(text, "\"content\"");
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\"') p++;

    const char *end = strchr(p, '\"');
    if (!end) return NULL;

    *out_len = (size_t)(end - p);
    return p;
}

/* -------------------------------------------------------------------------
 * Music response parsing
 *
 * A music SKILL response looks like:
 *   {"bizType":"SKILL","data":{"code":"music","general":{
 *     "action":"play","data":{"audios":[{
 *       "name":"开不了口","artist":"周杰伦","album":"范特西",
 *       "format":"mp3","url":"https://...mp3","duration":0,...
 *     }],"preTtsFlag":true
 *   },"template":{"name":"audio","version":"1.0"}}}}
 * ------------------------------------------------------------------------- */

/* Is this text a music SKILL response? "code" must be read inside the
 * envelope's data object; against the whole document it would match an outer
 * status code first. The literal is a fast path for the common spelling. */
static int is_music_response(const char *text)
{
    if (strstr(text, "\"code\":\"music\"")) return 1;
    if (!strstr(text, "music")) return 0;   /* cheap reject for NLG chatter */

    char *data = json_get_object_raw(text, "data");
    char  code[32];
    int   music = data &&
                  json_get_string(data, "code", code, sizeof(code)) == 0 &&
                  strcmp(code, "music") == 0;
    free(data);
    return music;
}

/* Print the metadata box and copy the trial-clip URL into url_out (empty if
 * there is none). Returns 1, or -1 if the payload could not be read — the
 * caller has already established this IS a music response, so every bail-out
 * here is a failure. */
static int try_parse_music(const char *text, char *url_out, size_t url_cap)
{
    int   rc      = -1;
    char *general = json_get_object_raw(text, "general");
    char *gdata   = general ? json_get_object_raw(general, "data") : NULL;
    char *audios  = gdata   ? json_get_array_raw(gdata, "audios")  : NULL;
    char *first   = NULL;

    url_out[0] = '\0';
    if (audios) {
        const char *open = strchr(audios, '{');
        if (open) {
            int depth = 0;
            for (const char *q = open; *q; q++) {
                if (*q == '{') depth++;
                else if (*q == '}' && --depth == 0) {
                    size_t olen = (size_t)(q - open + 1);
                    first = (char *)malloc(olen + 1);
                    if (first) { memcpy(first, open, olen); first[olen] = '\0'; }
                    break;
                }
            }
        }
    }
    if (!first) goto out;

    {
        char name[256]      = {0};
        char artist[256]    = {0};
        char album[256]     = {0};
        char format[32]     = {0};
        char audio_id[128]  = {0};
        char image_url[512] = {0};

        json_get_string(first, "name",     name,      sizeof(name));
        json_get_string(first, "artist",   artist,    sizeof(artist));
        json_get_string(first, "album",    album,     sizeof(album));
        json_get_string(first, "format",   format,    sizeof(format));
        json_get_string(first, "audioId",  audio_id,  sizeof(audio_id));
        json_get_string(first, "imageUrl", image_url, sizeof(image_url));
        json_get_string(first, "url",      url_out,   url_cap);

        /* A URL that cannot be downloaded is a parse failure, not "no URL". */
        if (url_out[0] &&
            strncmp(url_out, "http://", 7) != 0 &&
            strncmp(url_out, "https://", 8) != 0) {
            fprintf(stderr, "[text] rejecting non-http(s) audio URL\n");
            url_out[0] = '\0';
            goto out;
        }

        printf("\n");
        box_rule();
        box_center("MUSIC FOUND");
        box_rule();
        box_field("Song",    name[0]     ? name     : "(unknown)");
        box_field("Artist",  artist[0]   ? artist   : "(unknown)");
        box_field("Album",   album[0]    ? album    : "(unknown)");
        box_field("Format",  format[0]   ? format   : "?");
        box_field("AudioID", audio_id[0] ? audio_id : "?");
        box_rule();
        if (url_out[0])   printf("  Audio : %s\n", url_out);
        if (image_url[0]) printf("  Cover : %s\n", image_url);
        printf("\n");

        rc = 1;
    }

out:
    free(first);
    free(audios);
    free(gdata);
    free(general);
    return rc;
}

/* -------------------------------------------------------------------------
 * TAI callbacks
 * ------------------------------------------------------------------------- */

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    /* Try to parse as a music skill response. */
    if (is_music_response(msg->text)) {
        dc->music_state = try_parse_music(msg->text, dc->music_url,
                                          sizeof(dc->music_url));
        return;
    }

    /* For NLG lines, print only the content field. */
    size_t clen = 0;
    const char *content = nlg_extract_content(msg->text, msg->len, &clen);
    if (content && clen > 0) {
        fwrite(content, 1, clen, stdout);
        fflush(stdout);
        return;
    }

    /* Non-NLG, non-music text: print raw. */
    fwrite(msg->text, 1, msg->len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    (void)ctx; (void)msg; (void)ud;
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    if (msg->event_type == TAI_EVT_END) {
        dc->got_done = 1;
    } else if (msg->event_type == TAI_EVT_MCP_CMD) {
        const char *empty_result =
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}";
        tai_send_mcp_response(ctx, empty_result);
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    fprintf(stderr, "\n[disconnected: reason=%u close_code=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -------------------------------------------------------------------------
 * Download the mp3
 * ------------------------------------------------------------------------- */

static int download_mp3(const char *url, const char *outfile)
{
    /* The URL is server-supplied, so it must never reach a shell: fork+exec
     * passes it as one argv element that cannot be read as a command. */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        fprintf(stderr, "[main] refusing to download non-http(s) URL: %s\n", url);
        return -1;
    }

    printf("[main] Downloading: %s\n", url);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) { perror("[main] fork"); return -1; }
    if (pid == 0) {
        char *const argv[] = { (char *)"curl", (char *)"-fsSL",
                               (char *)"-o", (char *)outfile,
                               (char *)url, NULL };
        execvp("curl", argv);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { perror("[main] waitpid"); return -1; }
    }
    if (!WIFEXITED(status)) {
        fprintf(stderr, "[main] curl terminated abnormally\n");
        return -1;
    }
    int rc = WEXITSTATUS(status);
    if (rc == 127) {
        fprintf(stderr, "[main] curl not found in PATH\n");
        return -1;
    }
    if (rc != 0) {
        fprintf(stderr, "[main] curl failed (exit=%d)\n", rc);
        return -1;
    }

    printf("[main] Saved to: %s\n", outfile);
    printf("[main] Play with: afplay %s   (macOS)\n", outfile);
    printf("                  mpv %s      (Linux)\n", outfile);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

/* iot_client_config_t's credential fields are fixed-size char arrays; a value
 * that does not fit is rejected rather than overflowing them. */
static int cfg_set(char *dst, size_t cap, const char *src, const char *what)
{
    size_t n = strlen(src);
    if (n >= cap) {
        fprintf(stderr, "%s is too long (%zu bytes; max %zu)\n", what, n, cap - 1);
        return -1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

int main(int argc, char *argv[])
{
    const char *query      = (argc >= 2) ? argv[1] : DEFAULT_QUERY;
    const char *devid      = (argc >= 3) ? argv[2] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 4) ? argv[3] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 5) ? argv[4] : DEFAULT_LOCAL_KEY;

    printf("=== tai_music_play_demo ===\n");
    printf("Device ID : %s\n", devid);
    printf("Query     : %s\n", query);

    /* ---- 1. iot-sdk init ------------------------------------------------ */
    iot_init_default();
    iot_client_config_t iot_cfg = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = AY,
        .env              = PROD,
        .mqtt_disable_tls = false,
        .message_callback = NULL,
        .schema           = NULL,
        .schema_id        = NULL,
        .dp_state         = NULL,
    };
    if (cfg_set((char *)iot_cfg.devid,      sizeof(iot_cfg.devid),      devid,      "devid")      != 0 ||
        cfg_set((char *)iot_cfg.secret_key, sizeof(iot_cfg.secret_key), secret_key, "secret_key") != 0 ||
        cfg_set((char *)iot_cfg.local_key,  sizeof(iot_cfg.local_key),  local_key,  "local_key")  != 0)
        return 1;

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (!iot) { fprintf(stderr, "iot_client_init failed\n"); return 1; }

    /* ---- 2. Fetch session token ---------------------------------------- */
    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); return 1; }
    if (iot_client_get_session_token(iot, NULL, token, 4096) != 0 || token[0] == '\0') {
        fprintf(stderr, "iot_client_get_session_token failed\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }

    /* ---- 3. Parse token ------------------------------------------------ */
    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        fprintf(stderr, "Token parse failed\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    printf("[main] TAI server : %s:%u (SNI: %s)\n", cp.host, cp.port, cp.tls_sni);
    printf("[main] Client ID  : %s\n\n", cp.derived_client_id);

    free(token);
    iot_client_deinit(iot);

    /* ---- 4. Build TAI context ------------------------------------------ */
    const pal_t *pal = tai_pal_posix();

    demo_ctx_t dc;
    memset(&dc, 0, sizeof(dc));

    static const char SESSION_ATTRS[] =
        "{\"deviceMcp\":{\"supportCustomMCP\":true}}";
    static const char EVENT_USER_DATA[] =
        "{\"sys.workflow\":\"asr-llm-tts\"}";

    tai_config_t tai_cfg = {
        .host              = cp.host,
        .port              = cp.port,
        .tls_sni           = cp.tls_sni,
        .device_id         = cp.derived_client_id,
        .local_key         = local_key,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = (uint32_t)cp.biz_code,
        .biz_tag           = (uint64_t)cp.biz_tag,
        .agent_token       = cp.agent_token,
        .session_attrs_json   = SESSION_ATTRS,
        .event_user_data_json = EVENT_USER_DATA,
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
        .user_data         = &dc,
    };

    void *ctx_buf = pal->malloc(tai_ctx_size());
    if (!ctx_buf) { fprintf(stderr, "OOM\n"); return 1; }

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) { fprintf(stderr, "tai_ctx_init failed\n"); pal->free(ctx_buf); return 1; }

    tai_set_log_level(TAI_LOG_WARN);

    /* ---- 5-7. Connect, send, await response — with app-driven reconnect -- */
    int done = 0;
    while (!done) {
        printf("[main] Connecting to TAI server...\n");
        int rc = tai_connect(ctx);
        if (rc != TAI_OK) {
            fprintf(stderr, "tai_connect failed: %d\n", rc);
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

        /* ---- 6. Send a text query -------------------------------------- */
        printf("[main] Sending text: \"%s\"\nResponse: ", query);
        fflush(stdout);

        rc = tai_send_text(ctx, query, strlen(query));
        if (rc == TAI_OK) {
            int waited = 0;
            while (!dc.got_done && !dc.reconn.need_reconnect && waited < MAX_WAIT_MS) {
                usleep(100 * 1000);
                waited += 100;
            }
        } else {
            fprintf(stderr, "tai_send_text failed: %d\n", rc);
            demo_reconnect_signal(&dc.reconn, TAI_DISCONNECT_TRANSPORT, 0);
        }

        if (dc.got_done) {
            done = 1;
        } else if (!dc.reconn.need_reconnect) {
            printf("\n[main] Timed out after %d s\n", MAX_WAIT_MS / 1000);
            done = 1;
        } else {
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

    printf("\n");

cleanup:
    /* ---- 8. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);   /* joins the worker thread: dc is single-owner again */
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    /* ---- 9. Download the trial clip ------------------------------------ */
    int failed = !dc.got_done;
    if (dc.music_url[0]) {
        if (download_mp3(dc.music_url, OUTPUT_FILE) != 0) failed = 1;
    } else if (dc.music_state < 0) {
        fprintf(stderr, "[main] a music response arrived but could not be parsed\n");
        failed = 1;
    } else if (dc.music_state == 0) {
        printf("[main] no music skill response for this query\n");
    } else {
        printf("[main] music response carried no audio URL\n");
    }

    printf("\nDone.\n");
    return failed ? 1 : 0;
}
