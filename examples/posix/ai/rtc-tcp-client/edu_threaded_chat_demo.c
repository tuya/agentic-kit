/*
 * examples/threaded_chat/main.c -- Chat demo (iot-sdk + TAI).
 *
 * Uses tai_connect() which auto-starts a background receive thread.
 * The main thread sends a text query and sleeps until done.
 *
 * Build (from ai-tcp-sdk root, with iot-sdk at ../iot-sdk):
 *   cmake -B build -DTAI_PAL_OPENSSL=ON -DTAI_BUILD_EXAMPLES=ON \
 *         -DTAI_IOT_CHAT=ON
 *   cmake --build build
 *
 * Usage:
 *   ./build/tai_threaded_chat [devid] [secret_key] [local_key]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "tuya_ai.h"
#include "iot_client.h"

extern const pal_t *tai_pal_posix(void);

#define DEFAULT_DEVID      "6c7cfcabf3cfb30bc1edww"
#define DEFAULT_SECRET_KEY ">df698G^Ia;T(ikA"
#define DEFAULT_LOCAL_KEY  "<]'1Dvt'mF#jXIXZ"

#define MAX_WAIT_MS  60000

static volatile int g_running = 1;
static volatile int g_done    = 0;

static void on_sigint(int sig) { (void)sig; g_running = 0; }

/* -- Callbacks ---------------------------------------------------------- */

static void on_text(tai_ctx_t *ctx, const char *text, size_t len,
                    uint8_t stream_flag, void *ud)
{
    (void)ctx; (void)ud;
    fwrite(text, 1, len, stdout);
    fflush(stdout);
    if (stream_flag == TAI_STREAM_END || stream_flag == TAI_STREAM_ONE_SHOT)
        printf("\n");
}

static void on_audio(tai_ctx_t *ctx, const uint8_t *data, size_t len,
                     uint32_t sample_rate, uint16_t frame_duration,
                     void *ud)
{
    (void)ctx; (void)data; (void)len; (void)ud;
    (void)sample_rate; (void)frame_duration;
}

static void on_event(tai_ctx_t *ctx, uint16_t event_type,
                     const uint8_t *data, size_t len, void *ud)
{
    (void)data; (void)len; (void)ud;
    if (event_type == TAI_EVT_END) {
        g_done = 1;
    } else if (event_type == TAI_EVT_MCP_CMD) {
        tai_send_mcp_response(ctx,
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}");
    }
}

static void on_disconnect(tai_ctx_t *ctx, uint16_t code, void *ud)
{
    (void)ctx; (void)ud;
    fprintf(stderr, "\n[disconnected: code=%u]\n", (unsigned)code);
    g_running = 0;
    g_done    = 1;
}

/* -- Minimal JSON helpers (same as iot_chat) ----------------------------- */

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

/* -- Base64 decode (mbedTLS) ---------------------------------------- */

#include "mbedtls/base64.h"

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
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    const char *devid      = (argc >= 2) ? argv[1] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 3) ? argv[2] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 4) ? argv[3] : DEFAULT_LOCAL_KEY;

    printf("=== tai_threaded_chat demo ===\n");
    printf("Device ID : %s\n\n", devid);

    /* 1. Init iot-sdk */
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
    if (!iot) { fprintf(stderr, "iot_client_init failed\n"); return 1; }

    /* 2. Fetch session token */
    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); return 1; }

    int ret = iot_client_get_session_token(iot, NULL, token, 4096);
    if (ret != 0 || token[0] == '\0') {
        fprintf(stderr, "iot_client_get_session_token failed: %d\n", ret);
        free(token); iot_client_deinit(iot);
        return 1;
    }
    printf("[main] Session token acquired\n");

    /* 3. Parse token */
    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        fprintf(stderr, "Token parse failed\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    printf("[main] TAI server: %s:%u (SNI: %s)\n", cp.host, cp.port, cp.tls_sni);
    printf("[main] Client ID : %s\n\n", cp.derived_client_id);

    /* 4. Build TAI context */
    const pal_t *pal = tai_pal_posix();

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
        .pal               = pal,
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
    };

    void *ctx_buf = pal->malloc(tai_ctx_size());
    if (!ctx_buf) {
        fprintf(stderr, "OOM\n");
        free(token); iot_client_deinit(iot);
        return 1;
    }

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) {
        fprintf(stderr, "tai_ctx_init failed\n");
        pal->free(ctx_buf); free(token); iot_client_deinit(iot);
        return 1;
    }

    /* 5. Connect */
    printf("[main] Connecting to TAI server...\n");
    int rc = tai_connect(ctx);
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_connect failed: %d\n", rc);
        tai_ctx_deinit(ctx); pal->free(ctx_buf);
        free(token); iot_client_deinit(iot);
        return 1;
    }
    printf("[main] Connected.\n\n");

    /* 6. Send text query */
    const char *greeting = "Hello, AI! This is a threaded-chat demo.";
    printf("[main] Sending text: \"%s\"\n", greeting);
    printf("Response: ");
    fflush(stdout);

    rc = tai_send_text(ctx, greeting, strlen(greeting));
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_send_text failed: %d\n", rc);
        tai_disconnect(ctx); tai_ctx_deinit(ctx); pal->free(ctx_buf);
        free(token); iot_client_deinit(iot);
        return 1;
    }
    fprintf(stderr, "[main] tai_send_text returned %d\n", rc);

    /* 7. Wait for response (background thread drives receive) */
    int waited = 0;
    while (g_running && !g_done && waited < MAX_WAIT_MS) {
        usleep(100 * 1000);
        waited += 100;
    }

    if (!g_done)
        printf("\n[main] Timed out after %d s\n", MAX_WAIT_MS / 1000);

    /* 8. Shutdown */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    free(token);
    iot_client_deinit(iot);

    printf("\nDone.\n");
    return g_done ? 0 : 1;
}
