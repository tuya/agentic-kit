/*
 * text_chat_demo.c -- Simple text-chat demo using the rtc-tcp-client library.
 *
 * Bootstrap:
 *   1. iot-sdk init + login with devid / secret_key / local_key
 *   2. fetch session token via iot_client_get_session_token()
 *   3. parse connect_conf / session_conf out of the token
 *   4. build a TAI context, call tai_connect()
 *   5. send a text query and print the streamed response
 *
 * Build:
 *   cmake -S examples/posix -B build -DAGENTIC_KIT_BUILD_EXAMPLES=ON
 *   cmake --build build --target tai_text_chat_demo
 *
 * Usage:
 *   ./build/tai_text_chat_demo [devid] [secret_key] [local_key]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tuya_ai.h"
#include "iot_client.h"
#include "demo_json.h"
#include "demo_mcp.h"
#include "demo_reconnect.h"
#include "demo_text.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ----------------------------------------------------------- */

#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"

#define MAX_WAIT_MS 60000

/* -- Demo context ------------------------------------------------------- */

typedef struct {
    volatile int     got_done;   /* the AI response completed (TAI_EVT_END)  */
    demo_reconnect_t reconn;      /* app-side reconnect policy/state          */
} demo_ctx_t;

/* -------------------------------------------------------------------------
 * TAI callbacks
 * ------------------------------------------------------------------------- */

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx; (void)ud;

    /* For NLG lines, print only the content field, escapes decoded. Handles the
     * empty terminator line too — which is NLG, so it must not fall through. */
    if (nlg_print_content(msg->text, msg->len)) return;

    /* Non-NLG text: print raw. */
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
        /* This demo exposes no tools, but it does declare MCP support, so it
         * still owes the server a well-formed answer. See demo_mcp.h. */
        demo_mcp_reply_no_tools(ctx, msg);
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    fprintf(stderr, "\n[disconnected: reason=%u close_code=%u]\n",
            (unsigned)msg->reason, (unsigned)msg->close_code);
    /* Runs on the worker thread: only flag — the main loop reconnects. */
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *devid      = (argc >= 2) ? argv[1] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 3) ? argv[2] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 4) ? argv[3] : DEFAULT_LOCAL_KEY;

    printf("=== tai_text_chat_demo ===\n");
    printf("Device ID : %s\n", devid);

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
        .schema           = NULL,   /* restart path: fill from persisted storage if used */
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

    tai_set_log_level(TAI_LOG_WARN);

    /* ---- 5-7. Connect, send, await response — with app-driven reconnect --
     *
     * on_disconnect runs on the worker thread and only flags dc.reconn (it must
     * not self-disconnect). This owning thread does tai_disconnect() +
     * tai_connect() with exponential backoff + a circuit breaker — the correct
     * response to the fail-fast model (see demo_reconnect.h). */
    const char *question = "Hello, how are you?";
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
        printf("[main] Sending text: \"%s\"\nResponse: ", question);
        fflush(stdout);

        rc = tai_send_text(ctx, question, strlen(question));
        if (rc == TAI_OK) {
            /* ---- 7. Wait for AI response (or a disconnect) ------------- */
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
            fprintf(stderr, "tai_send_text failed: %d\n", rc);
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

cleanup:
    /* ---- 8. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    printf("\nDone.\n");
    return dc.got_done ? 0 : 1;
}
