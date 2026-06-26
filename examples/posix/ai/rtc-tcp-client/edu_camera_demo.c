/*
 * edu_camera_demo.c -- Image-understanding demo using the rtc-tcp-client library.
 *
 * Sends a text prompt + image to the Tuya AI Foundation in one event and
 * receives a streamed text + TTS-audio response.
 *
 * Flow:
 *   1. iot_client_init()                  -- authenticate device with Tuya cloud
 *   2. iot_client_get_session_token()     -- fetch AI server connection config
 *   3. parse_token()                      -- decode JSON -> host/port/client_id
 *   4. tai_ctx_init() + tai_connect()     -- TLS + protocol handshake (v2.1)
 *   5. tai_send_image_with_text()         -- send text prompt + image in one event
 *   6. tai_poll() loop (60 s)             -- receive streamed response
 *   7. Print latency stats                -- first-text / first-audio / audio-done
 *   8. tai_disconnect() + cleanup
 *
 * Build:
 *   cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
 *   cmake --build build --target tai_edu_camera_demo
 *
 * Usage:
 *   ./build/tai_edu_camera_demo [img_path] [prompt] [audio_path] [devid] [secret_key] [local_key]
 *
 * Defaults: test.jpg / "image_recognition" / output_tts.pcm / built-in device
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "mbedtls/base64.h"

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_reconnect.h"

extern const pal_t *tai_pal_posix(void);

#define DEFAULT_DEVID      "6c287c33f24ff6a2b0jruc"
#define DEFAULT_SECRET_KEY "-B+a]RSc2WB}&v4)"
#define DEFAULT_LOCAL_KEY  "_=i7?q{X}9M#[/tG"


#define OUTPUT_FILE   "output_tts.pcm"
#define MAX_WAIT_MS   60000
#define MAX_IMG_SIZE  (10 * 1024 * 1024)

/* -- Receive context ---------------------------------------------------- */
typedef struct {
    FILE        *audio_fp;
    char         output_path[256];

    volatile int got_done;
    int          got_first_text;
    int          got_first_audio;

    int64_t      send_end_us;
    int64_t      first_text_us;
    int64_t      first_audio_us;
    int64_t      audio_end_us;
    demo_reconnect_t reconn;       /* app-side reconnect policy/state */
} demo_ctx_t;

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

/* -- TAI callbacks ------------------------------------------------------ */

static void on_text(tai_ctx_t *ctx,
                    const tai_text_msg_t *msg,
                    void *user_data)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)user_data;

    if (!dc->got_first_text) {
        dc->first_text_us  = now_us();
        dc->got_first_text = 1;
    }
    printf("[Text] ");
    fwrite(msg->text, 1, msg->len, stdout);
    if (msg->stream_flag == TAI_STREAM_END || msg->stream_flag == TAI_STREAM_ONE_SHOT)
        printf("\n");
    fflush(stdout);
}

static void on_audio(tai_ctx_t *ctx,
                     const tai_audio_msg_t *msg,
                     void *user_data)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)user_data;

    if (!dc->got_first_audio) {
        dc->first_audio_us  = now_us();
        dc->got_first_audio = 1;
        printf("[Audio stream started]\n");
    }
    if (dc->audio_fp && msg->data && msg->len > 0)
        fwrite(msg->data, 1, msg->len, dc->audio_fp);
}

static void on_event(tai_ctx_t *ctx,
                     const tai_event_msg_t *msg,
                     void *user_data)
{
    demo_ctx_t *dc = (demo_ctx_t *)user_data;

    if (msg->event_type == TAI_EVT_PAYLOADS_END) {
        dc->audio_end_us = now_us();
    } else if (msg->event_type == TAI_EVT_END) {
        if (dc->audio_end_us == 0)
            dc->audio_end_us = now_us();
        dc->got_done = 1;
    } else if (msg->event_type == TAI_EVT_MCP_CMD) {
        const char *empty =
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}";
        tai_send_mcp_response(ctx, empty);
    }
}

static void on_disconnect(tai_ctx_t *ctx,
                          const tai_disconnect_msg_t *msg,
                          void *user_data)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)user_data;
    fprintf(stderr, "\n[TAI disconnected: reason=%u close_code=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
    /* Runs on the worker thread: only flag — the main loop reconnects. */
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -- Image format detection --------------------------------------------- */

static uint8_t detect_image_format(const uint8_t *data, size_t len)
{
    if (len >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
        data[2] == 0x4E && data[3] == 0x47)
        return TAI_IMG_PNG;
    return TAI_IMG_JPEG;
}

/* -- Minimal JSON helpers (same pattern as iot_chat) --------------------- */

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

/* -- Base64 decode (OpenSSL EVP) ---------------------------------------- */

static char *b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t dlen = 0;
    if (mbedtls_base64_decode(NULL, 0, &dlen,
                               (const unsigned char *)encoded, elen) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
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

/* -- Parse token -> TAI connection params ------------------------------- */

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
        size_t  dl = 0;
        char   *decoded = b64_decode(raw_token, &dl);
        if (decoded && dl > 0 && decoded[0] == '{') {
            json = decoded;
        } else {
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

    json_array_first_string(conn, "hosts", p->host, sizeof(p->host));

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

/* -- main --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *img_path   = (argc >= 2) ? argv[1] : "res/test.jpg";
    const char *prompt     = (argc >= 3) ? argv[2] : "image_recognition";
    const char *audio_path = (argc >= 4) ? argv[3] : OUTPUT_FILE;
    const char *devid      = (argc >= 5) ? argv[4] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 6) ? argv[5] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 7) ? argv[6] : DEFAULT_LOCAL_KEY;

    printf("=== tai_edu_camera demo ===\n");
    printf("Device ID  : %s\n", devid);
    printf("Image Path : %s\n", img_path);
    printf("Prompt     : %s\n", prompt);
    printf("Audio Out  : %s\n", audio_path);
    printf("\n");

    /* -- 1. Load image file --------------------------------------------- */
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        fprintf(stderr, "[main] Cannot open image: %s\n", img_path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > MAX_IMG_SIZE) {
        fclose(fp);
        fprintf(stderr, "[main] Image size invalid (%ld bytes, limit %d MB)\n",
                fsize, MAX_IMG_SIZE / (1024 * 1024));
        return 1;
    }

    uint8_t *img_data = (uint8_t *)malloc((size_t)fsize);
    if (!img_data) { fclose(fp); return 1; }
    size_t nread = fread(img_data, 1, (size_t)fsize, fp);
    fclose(fp);
    if (nread != (size_t)fsize) {
        free(img_data);
        fprintf(stderr, "[main] Partial image read\n");
        return 1;
    }

    uint8_t img_format = detect_image_format(img_data, nread);
    printf("[main] Image: %s  %zu bytes  format=%s\n",
           img_path, nread, img_format == TAI_IMG_PNG ? "PNG" : "JPEG");

    /* -- 2. Init iot-sdk ------------------------------------------------ */
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
        free(img_data);
        return 1;
    }
    printf("[main] iot-sdk initialized\n");

    /* -- 3. Fetch session token ----------------------------------------- */
    char *token = (char *)malloc(4096);
    if (!token) {
        free(img_data); iot_client_deinit(iot);
        return 1;
    }
    memset(token, 0, 4096);

    int ret = iot_client_get_session_token(iot, NULL, token, 4096);
    if (ret != 0 || token[0] == '\0') {
        fprintf(stderr, "[main] iot_client_get_session_token failed: %d\n", ret);
        free(token); free(img_data); iot_client_deinit(iot);
        return 1;
    }
    printf("[main] Session token acquired (len=%zu)\n", strlen(token));

    /* -- 4. Parse token -> TAI connection params ------------------------ */
    tai_conn_params_t conn;
    if (parse_token(token, &conn) != 0) {
        const char *h  = getenv("TAI_HOST");
        const char *ps = getenv("TAI_PORT");
        const char *s  = getenv("TAI_SNI");
        const char *c  = getenv("TAI_CLIENT_ID");
        if (!h || !ps || !c) {
            fprintf(stderr,
                    "[main] Token parse failed; set TAI_HOST, TAI_PORT, "
                    "TAI_CLIENT_ID env vars as fallback\n");
            free(token); free(img_data); iot_client_deinit(iot);
            return 1;
        }
        strncpy(conn.host,              h,          sizeof(conn.host) - 1);
        strncpy(conn.tls_sni,           s ? s : h,  sizeof(conn.tls_sni) - 1);
        strncpy(conn.derived_client_id, c,          sizeof(conn.derived_client_id) - 1);
        conn.port = (uint16_t)atoi(ps);
    }
    if (conn.biz_code == 0) conn.biz_code = 65537;
    if (conn.biz_tag  == 0) conn.biz_tag  = 119;

    printf("[main] TAI server  : %s:%u  (SNI: %s)\n",
           conn.host, conn.port, conn.tls_sni);
    printf("[main] Client ID   : %s\n\n", conn.derived_client_id);

    /* -- 5. Build TAI config + allocate context ------------------------- */
    demo_ctx_t dc;
    memset(&dc, 0, sizeof(dc));
    snprintf(dc.output_path, sizeof(dc.output_path), "%s", audio_path);

    dc.audio_fp = fopen(dc.output_path, "wb");
    if (!dc.audio_fp)
        fprintf(stderr, "[main] Warning: cannot create %s\n", dc.output_path);

    const pal_t *pal = tai_pal_posix();

    tai_config_t tai_cfg = {
        .host              = conn.host,
        .port              = conn.port,
        .tls_sni           = conn.tls_sni,
        .device_id = conn.derived_client_id,
        .local_key         = local_key,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = (uint32_t)conn.biz_code,
        .biz_tag           = (uint64_t)conn.biz_tag,
        .agent_token       = conn.agent_token,
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
        .user_data         = &dc,
    };

    size_t ctx_bytes = tai_ctx_size();
    void  *ctx_buf   = pal->malloc(ctx_bytes);
    if (!ctx_buf) {
        fprintf(stderr, "[main] OOM for TAI context (%zu bytes)\n", ctx_bytes);
        if (dc.audio_fp) fclose(dc.audio_fp);
        free(token); free(img_data); iot_client_deinit(iot);
        return 1;
    }

    tai_ctx_t *tai = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!tai) {
        fprintf(stderr, "[main] tai_ctx_init failed\n");
        pal->free(ctx_buf);
        if (dc.audio_fp) fclose(dc.audio_fp);
        free(token); free(img_data); iot_client_deinit(iot);
        return 1;
    }

    /* -- 6-8. Connect, send image, await response — with app-driven reconnect.
     *
     * on_disconnect runs on the worker thread and only flags dc.reconn (it must
     * not self-disconnect). This owning thread does tai_disconnect() +
     * tai_connect() with exponential backoff + a circuit breaker — the correct
     * response to the fail-fast model (see demo_reconnect.h). */
    int done = 0;
    while (!done) {
        /* -- 6. Connect (TLS + protocol handshake) ---------------------- */
        printf("[main] Connecting to TAI server...\n");
        int rc = tai_connect(tai);
        if (rc != TAI_OK) {
            fprintf(stderr, "[main] tai_connect failed: %d\n", rc);
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[main] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                done = 1;
                break;
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

        /* -- 7. Send text prompt + image -------------------------------- */
        printf("[main] Sending prompt: \"%s\"\n", prompt);
        printf("[main] Sending image: %zu bytes (%s)\n", nread,
               img_format == TAI_IMG_PNG ? "PNG" : "JPEG");

        rc = tai_send_image_with_text(tai,
                                      prompt, strlen(prompt),
                                      img_data, nread,
                                      img_format, 0, 0);

        dc.send_end_us = now_us();

        if (rc == TAI_OK) {
            printf("[main] Send complete.\n");

            /* -- 8. Wait for AI response (or a disconnect) ------------- */
            printf("[main] Waiting for AI response...\n\n");
            int wait_ms = 0;
            while (!dc.got_done && !dc.reconn.need_reconnect && wait_ms < MAX_WAIT_MS) {
                usleep(100 * 1000);
                wait_ms += 100;
            }
        } else {
            /* An app-thread send failure is reported synchronously here — the
             * SDK does NOT fire on_disconnect for it (only the worker's own
             * ping does). The TX stream may be desynced, so treat it as a
             * transport fault: request a reconnect so the teardown path below
             * rebuilds the link instead of exiting as a benign timeout. */
            fprintf(stderr, "[main] Send failed: %d\n", rc);
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
            tai_disconnect(tai);
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

    free(img_data);
    img_data = NULL;

    /* -- 9. Close output file ------------------------------------------- */
    if (dc.audio_fp) {
        fclose(dc.audio_fp);
        dc.audio_fp = NULL;
    }

    /* -- 10. Latency stats ---------------------------------------------- */
    printf("\n========== Latency Stats ==========\n");
    if (dc.got_first_text)
        printf("Send end    -> first text:  %.1f ms\n",
               (double)(dc.first_text_us - dc.send_end_us) / 1000.0);
    else
        printf("Send end    -> first text:  (none received)\n");

    if (dc.got_first_audio)
        printf("Send end    -> first audio: %.1f ms\n",
               (double)(dc.first_audio_us - dc.send_end_us) / 1000.0);
    else
        printf("Send end    -> first audio: (none received)\n");

    if (dc.got_first_audio && dc.audio_end_us)
        printf("First audio -> audio done:  %.1f ms\n",
               (double)(dc.audio_end_us - dc.first_audio_us) / 1000.0);
    else
        printf("First audio -> audio done:  (incomplete)\n");
    printf("====================================\n");

    if (dc.got_done)
        printf("[main] TTS audio saved to: %s\n", dc.output_path);
    else
        printf("[main] Response timed out after %d s\n", MAX_WAIT_MS / 1000);

    /* -- 11. Cleanup ---------------------------------------------------- */
    tai_disconnect(tai);
    tai_ctx_deinit(tai);
    pal->free(ctx_buf);

    free(token);
    iot_client_deinit(iot);

    printf("\nDone.\n");
    return dc.got_done ? 0 : 1;
}
