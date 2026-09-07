/*
 * long_connection_demo.c -- Long-lived connection demo (TEMPORARY feature test).
 *
 * Confirms the ConnectionRefresh feature keeps a TAI connection alive well past
 * the server-side lifetime window: it connects once and holds the link open for
 * a configurable duration (default 100 min, i.e. > 1.5 h), letting the SDK's
 * background worker drive both keepalive (Ping/Pong) and the periodic
 * ConnectionRefreshRequest (every TAI_CONN_REFRESH_INTERVAL_MS, default 30 min).
 *
 * It does NOT reconnect: any on_disconnect is treated as a FAILURE, because the
 * whole point is to prove the link never drops on its own. To make the liveness
 * observable it sends one short text query every few minutes and checks the
 * response still comes back.
 *
 * Build:
 *   cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
 *   cmake --build build --target long_connection_demo
 *
 * Usage:
 *   ./build/long_connection_demo [devid] [secret_key] [local_key] [minutes]
 *   minutes: how long to hold the connection (default 100).
 *
 * Exit status: 0 if the link survived the whole run, 1 if it dropped or a probe
 * failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_json.h"
#include "demo_mcp.h"
#include "demo_text.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ----------------------------------------------------------- */

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"

#define DEFAULT_RUN_MINUTES 100         /* > 1.5 h, spans >=3 refresh intervals */
#define PROBE_INTERVAL_SEC  300         /* send a liveness probe every 5 min    */
#define PROBE_WAIT_MS       30000       /* max wait for a probe response        */

/* -- Demo context ------------------------------------------------------- */

typedef struct {
    volatile int got_done;       /* current probe's response completed        */
    volatile int disconnected;   /* worker fired on_disconnect (a FAILURE)    */
    uint8_t      reason;
    uint16_t     close_code;
} demo_ctx_t;

/* -------------------------------------------------------------------------
 * TAI callbacks (all run on the worker thread)
 * ------------------------------------------------------------------------- */

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;
    if (nlg_print_content(msg->text, msg->len)) return;
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
        demo_mcp_reply_no_tools(ctx, msg);
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    dc->reason       = msg->reason;
    dc->close_code   = msg->close_code;
    dc->disconnected = 1;   /* the main loop treats this as a run failure */
    fprintf(stderr, "\n[FAIL] disconnected: reason=%u close_code=%u\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
}

/* -- Wall-clock helper -------------------------------------------------- */

static uint64_t now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec;
}

static void hhmmss(uint64_t s, char *out, size_t cap)
{
    snprintf(out, cap, "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *devid      = (argc >= 2) ? argv[1] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 3) ? argv[2] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 4) ? argv[3] : DEFAULT_LOCAL_KEY;
    int run_minutes = (argc >= 5) ? atoi(argv[4]) : DEFAULT_RUN_MINUTES;
    if (run_minutes <= 0) run_minutes = DEFAULT_RUN_MINUTES;

    printf("=== long_connection_demo (temporary: ConnectionRefresh) ===\n");
    printf("Device ID    : %s\n", devid);
    printf("Hold duration: %d min (%.1f h)\n", run_minutes, run_minutes / 60.0);
    printf("A ConnectionRefreshRequest is sent every 30 min by the worker;\n"
           "any on_disconnect during the run is a FAILURE.\n\n");

    /* ---- 1. iot-sdk init ----------------------------------------------- */
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
    if (demo_copy_field((char *)iot_cfg.devid,      sizeof(iot_cfg.devid),      devid,      "devid")      != 0 ||
        demo_copy_field((char *)iot_cfg.secret_key, sizeof(iot_cfg.secret_key), secret_key, "secret_key") != 0 ||
        demo_copy_field((char *)iot_cfg.local_key,  sizeof(iot_cfg.local_key),  local_key,  "local_key")  != 0)
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

    tai_set_log_level(TAI_LOG_INFO);   /* INFO surfaces the worker's refresh log */

    /* ---- 5. Connect ONCE (no reconnect: a drop is a failure) ----------- */
    printf("[main] Connecting to TAI server...\n");
    int rc = tai_connect(ctx);
    if (rc != TAI_OK) {
        fprintf(stderr, "tai_connect failed: %d\n", rc);
        tai_ctx_deinit(ctx); pal->free(ctx_buf);
        return 1;
    }
    printf("[main] Connected. Holding the link for %d min...\n\n", run_minutes);

    /* ---- 6. Hold the link, probing periodically ------------------------ */
    const uint64_t start   = now_sec();
    const uint64_t run_sec = (uint64_t)run_minutes * 60u;
    uint64_t next_probe    = 0;                 /* probe immediately, then every 5 min */
    int probe_no  = 0;
    int failed    = 0;

    while (!dc.disconnected) {
        uint64_t elapsed = now_sec() - start;
        if (elapsed >= run_sec) break;          /* survived the full run */

        if (elapsed >= next_probe) {
            char ts[16]; hhmmss(elapsed, ts, sizeof(ts));
            probe_no++;
            printf("[t+%s] liveness probe #%d ... ", ts, probe_no);
            fflush(stdout);

            dc.got_done = 0;
            rc = tai_send_text(ctx, "ping", 4);
            if (rc != TAI_OK) {
                /* App-thread send failure: the SDK does not fire on_disconnect
                 * for it — treat the desynced TX stream as a run failure. */
                fprintf(stderr, "tai_send_text failed: %d\n", rc);
                failed = 1;
                break;
            }
            int waited = 0;
            while (!dc.got_done && !dc.disconnected && waited < PROBE_WAIT_MS) {
                usleep(100 * 1000);
                waited += 100;
            }
            if (dc.disconnected) break;
            if (!dc.got_done) {
                printf("no response within %d s\n", PROBE_WAIT_MS / 1000);
                failed = 1;
                break;
            }
            printf("ok\n");
            next_probe = elapsed + PROBE_INTERVAL_SEC;
        }

        usleep(500 * 1000);   /* 0.5 s poll; keeps shutdown/probe latency low */
    }

    /* ---- 7. Verdict ---------------------------------------------------- */
    uint64_t held = now_sec() - start;
    char ts[16]; hhmmss(held, ts, sizeof(ts));
    int ok = !dc.disconnected && !failed;
    printf("\n=== Result: link held for %s (%d probes) — %s ===\n",
           ts, probe_no, ok ? "PASS" : "FAIL");
    if (dc.disconnected)
        printf("    dropped by on_disconnect (reason=%u code=%u)\n",
               (unsigned)dc.reason, (unsigned)dc.close_code);

    /* ---- 8. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    return ok ? 0 : 1;
}
