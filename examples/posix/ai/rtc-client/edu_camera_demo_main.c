/**
 * @file edu_camera_demo_main.c
 * @brief edu-camera-demo entry point.
 *
 * Usage:
 *   ./edu-camera-demo [img_path] [prompt] [audio_path] [devid] [secret_key] [local_key]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "iot_client.h"
#include "edu_camera_demo.h"

#define DEFAULT_DEVID      "6c287c33f24ff6a2b0jruc"
#define DEFAULT_SECRET_KEY "-B+a]RSc2WB}&v4)"
#define DEFAULT_LOCAL_KEY  "_=i7?q{X}9M#[/tG"

static void gen_session_id(char *buf, size_t cap)
{
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    snprintf(buf, cap, "open_img_%08x-%04x-%04x-%04x-%04x%08x",
             (unsigned)time(NULL),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)(rand() & 0xFFFF),
             (unsigned)rand());
}

int main(int argc, char *argv[])
{
    const char *img_path   = (argc >= 2) ? argv[1] : "res/test.jpg";
    const char *prompt     = (argc >= 3) ? argv[2] : "image_recognition";
    //const char *prompt     = (argc >= 3) ? argv[2] : "test";
    const char *audio_path = (argc >= 4) ? argv[3] : "output_tts.pcm";
    const char *devid      = (argc >= 5) ? argv[4] : DEFAULT_DEVID;
    const char *secret_key = (argc >= 6) ? argv[5] : DEFAULT_SECRET_KEY;
    const char *local_key  = (argc >= 7) ? argv[6] : DEFAULT_LOCAL_KEY;

    printf("=== edu-camera-demo ===\n");
    printf("Device ID  : %s\n", devid);
    printf("Image Path : %s\n", img_path);
    printf("Prompt     : %s\n", prompt);
    printf("\n");

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
        .schema           = NULL,   /* restart path: fill from persisted storage if used */
        .schema_id        = NULL,
        .dp_state         = NULL,
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

    char *token = malloc(2048);
    memset(token, 0, 2048);
    int ret = iot_client_get_session_token(client, NULL, token, 2048);
    if (ret != 0 || token[0] == ' ') {
        fprintf(stderr, "[main] iot_client_get_session_token failed: %d\n", ret);
        iot_client_deinit(client);
        return 1;
    }
    printf("[main] Session token acquired (len=%zu)\n", strlen(token));

    char session_id[128];
    gen_session_id(session_id, sizeof(session_id));

    ret = demo_image_understand_run(token, local_key, session_id, img_path, prompt, audio_path);

    free(token);
    iot_client_deinit(client);

    return ret == 0 ? 0 : 1;
}
