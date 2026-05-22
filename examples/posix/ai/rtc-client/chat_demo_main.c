/**
 * @file main.c
 * @brief agentic-kit-demo entry point.
 *
 * Usage:
 *   ./agentic-kit-demo [input.pcm] [devid] [secret_key] [local_key]
 *
 * Steps:
 *  1. Parse argv (fall back to embedded defaults)
 *  2. iot_client_init() with credentials + log callback
 *  3. iot_client_get_session_token(NULL, &response)
 *  4. Generate a UUID-style session_id
 *  5. demo_chat_run(token, local_key, session_id, input_pcm)
 *  6. Cleanup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "iot_client.h"
#include "chat_demo.h"

/* ── Default credentials (override via argv) ─────────────────────────────── */
#define DEFAULT_DEVID      "6c7cfcabf3cfb30bc1edww"
#define DEFAULT_SECRET_KEY ">df698G^Ia;T(ikA"
#define DEFAULT_LOCAL_KEY  "<]'1Dvt'mF#jXIXZ"


/* ── UUID-style session ID ───────────────────────────────────────────────── */
static void gen_session_id(char *buf, size_t cap)
{
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    snprintf(buf, cap, "open_chat_%08x-%04x-%04x-%04x-%04x%08x",
             (unsigned)time(NULL),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)rand());
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *input_pcm  = (argc >= 2) ? argv[1] : NULL;
    const char *devid      = (argc >= 3) ? argv[2] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 4) ? argv[3] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 5) ? argv[4] : DEFAULT_LOCAL_KEY;

    printf("=== agentic-kit-demo ===\n");
    printf("Device ID  : %s\n", devid);
    printf("Input PCM  : %s\n", input_pcm ? input_pcm : "(text mode)");
    printf("\n");

    /* ── 1. Init iot-sdk ─────────────────────────────────────────────────── */
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[main] iot_init_default failed\n");
        return 1;
    }

    iot_client_config_t config = {
        .devid            = {0},
        .secret_key       = {0},
        .local_key        = {0},
        .region           = AY,
        .env              = PROD,
        .mqtt_disable_tls = false,
        .message_callback = NULL,
    };
    memcpy((char *)config.devid,      devid,      strlen(devid));
    memcpy((char *)config.secret_key, secret_key, strlen(secret_key));
    memcpy((char *)config.local_key,  local_key,  strlen(local_key));

    iot_client_t *client = iot_client_init(&config);
    if (!client) {
        fprintf(stderr, "[main] iot_client_init failed\n");
        return 1;
    }
    printf("[main] iot-sdk initialised\n");

    /* ── 2. Fetch session token ──────────────────────────────────────────── */
    char  *token = malloc(2048);
    memset(token, 0, 2048);
    int ret = iot_client_get_session_token(client, NULL, token, 2048);
    if (ret != 0 || token[0] == ' ') {
        fprintf(stderr, "[main] iot_client_get_session_token failed: %d\n", ret);
        iot_client_deinit(client);
        return 1;
    }
    printf("[main] Session token acquired (len=%zu)\n", strlen(token));

    /* ── 3. Generate session ID ──────────────────────────────────────────── */
    char session_id[128];
    gen_session_id(session_id, sizeof(session_id));

    /* ── 4. Run chat demo ────────────────────────────────────────────────── */
    ret = demo_chat_run(token, local_key, session_id, input_pcm);

    /* ── 5. Cleanup ──────────────────────────────────────────────────────── */
    free(token);
    iot_client_deinit(client);
    
    return ret == 0 ? 0 : 1;
}
