/*
 * examples/text_query/main.c — End-to-end text query example.
 *
 * Sends a text question to the Tuya AI backend and prints the streamed
 * text response to stdout.
 *
 * Before running, set these environment variables (from your MQTT
 * protocol-9000 response and device credentials):
 *
 *   TAI_HOST              e.g. "m1.tuyacn.com"
 *   TAI_PORT              e.g. "443"
 *   TAI_SNI               e.g. "m1.tuyacn.com"
 *   TAI_CLIENT_ID         derived_client_id from MQTT proto-9000
 *   TAI_LOCAL_KEY         device localKey
 *   TAI_QUERY             text to send (default: "Hello, how are you?")
 *
 * Build:
 *   cmake -B build -DTAI_PAL_OPENSSL=ON -DTAI_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/tai_text_query
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "tuya_ai.h"

/* Forward-declare the factory from the chosen PAL */
extern const pal_t *tai_pal_posix(void);

/* -------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------- */
static volatile int g_running = 1;
static volatile int g_done    = 0;

static void on_sigint(int sig) { (void)sig; g_running = 0; }

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */
static void on_text(tai_ctx_t *ctx,
                    const char *text, size_t len,
                    uint8_t stream_flag,
                    void *user_data)
{
    (void)ctx; (void)user_data;

    /* Write text fragment to stdout without a trailing newline so streaming
     * looks natural.  stream_flag == TAI_STREAM_END signals end of response. */
    fwrite(text, 1, len, stdout);
    fflush(stdout);

    if (stream_flag == TAI_STREAM_END || stream_flag == TAI_STREAM_ONE_SHOT) {
        printf("\n");
        g_done = 1;
    }
}

static void on_audio(tai_ctx_t *ctx,
                     const uint8_t *data, size_t len,
                     uint32_t sample_rate, uint16_t frame_duration,
                     void *user_data)
{
    (void)ctx; (void)data; (void)len; (void)user_data;
    (void)sample_rate; (void)frame_duration;
    /* Ignore TTS audio in this text-only example */
}

static void on_event(tai_ctx_t *ctx,
                     uint16_t event_type,
                     const uint8_t *data, size_t len,
                     void *user_data)
{
    (void)ctx; (void)data; (void)len; (void)user_data;
    if (event_type == TAI_EVT_END) {
        g_done = 1;
    } else if (event_type == TAI_EVT_MCP_CMD) {
        /* Minimal MCP response: empty tools list */
        const char *empty_result =
            "{\"jsonrpc\":\"2.0\",\"id\":1,"
            "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"\"}]}}";
        tai_send_mcp_response(ctx, empty_result);
    }
}

static void on_disconnect(tai_ctx_t *ctx, uint16_t error_code, void *user_data)
{
    (void)ctx; (void)user_data;
    fprintf(stderr, "\n[disconnected: code=%u]\n", (unsigned)error_code);
    g_running = 0;
    g_done    = 1;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : def;
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    /* --- Configuration -------------------------------------------------- */
    const char *host       = env_or("TAI_HOST",      "m1.tuyacn.com");
    const char *port_str   = env_or("TAI_PORT",      "443");
    const char *sni        = env_or("TAI_SNI",       host);
    const char *client_id  = env_or("TAI_CLIENT_ID", "");
    const char *local_key  = env_or("TAI_LOCAL_KEY", "");
    const char *query      = env_or("TAI_QUERY",     "Hello, how are you?");
    uint16_t    port       = (uint16_t)atoi(port_str);

    printf("Connecting to %s:%u (SNI: %s)\n", host, port, sni);

    /* --- Build TAI config ----------------------------------------------- */
    tai_config_t cfg = {
        .host              = host,
        .port              = port,
        .tls_sni           = sni,
        .device_id = client_id,
        .local_key         = local_key,
        .protocol_version  = TAI_VER_21,
        .client_type       = TAI_CLIENT_DEVICE,
        .sign_level        = TAI_SIGN_HMAC_SHA256,
        .biz_code          = 65537,
        .biz_tag           = 119,
        .pal               = tai_pal_posix(),
        .on_text           = on_text,
        .on_audio          = on_audio,
        .on_event          = on_event,
        .on_disconnect     = on_disconnect,
        .user_data         = NULL,
    };

    /* --- Allocate context (static, no heap needed) ---------------------- */
    static uint8_t ctx_mem[1];  /* placeholder; real size from tai_ctx_size() */
    /* For real use, allocate tai_ctx_size() bytes.  Here we use the heap: */
    size_t ctx_bytes = tai_ctx_size();
    void *ctx_buf = cfg.pal->malloc(ctx_bytes);
    if (!ctx_buf) {
        fprintf(stderr, "Out of memory allocating context (%zu bytes)\n",
                ctx_bytes);
        return 1;
    }
    (void)ctx_mem;

    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &cfg);
    if (!ctx) {
        fprintf(stderr, "tai_ctx_init failed\n");
        cfg.pal->free(ctx_buf);
        return 1;
    }

    /* --- Connect -------------------------------------------------------- */
    printf("Establishing TLS connection + protocol handshake...\n");
    int rc = tai_connect(ctx);
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_connect failed: %d\n", rc);
        tai_ctx_deinit(ctx);
        cfg.pal->free(ctx_buf);
        return 1;
    }
    printf("Connected.\n");

    /* --- Send query ----------------------------------------------------- */
    printf("Query: %s\n", query);
    printf("Response: ");
    fflush(stdout);

    rc = tai_send_text(ctx, query, strlen(query));
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_send_text failed: %d\n", rc);
        tai_disconnect(ctx);
        tai_ctx_deinit(ctx);
        cfg.pal->free(ctx_buf);
        return 1;
    }

    /* --- Receive loop --------------------------------------------------- */
    while (g_running && !g_done) {
        usleep(100 * 1000);
    }

    /* --- Shutdown ------------------------------------------------------- */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    cfg.pal->free(ctx_buf);

    printf("\nDone.\n");
    return 0;
}
