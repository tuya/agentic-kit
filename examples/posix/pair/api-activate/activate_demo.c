/**
 * @file activate_demo.c
 * @brief API-based activation demo: token is obtained externally via Tuya
 *        OpenAPI and passed directly to the device.
 *
 * Flow:
 *  1. Receive the pairing token from the caller (originally from OpenAPI).
 *  2. iot_client_init_on_boarding_with_token() -- activate device with token.
 *  3. iot_client_get_session_token() -- verify cloud connectivity.
 */

#include "activate_demo.h"
#include "iot_client.h"

#include <stdio.h>
#include <string.h>

#define TAG "api_activate"

int demo_activate_run(const char *token,
                      const char *uuid, const char *authkey,
                      const char *product_key, const char *firmware_key)
{
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return -1;
    }
    log_set_level(LOG_DEBUG);

    iot_on_boarding_config_t cfg = {
        .timeout_ms       = 10000,
        .env              = PROD,
        .mqtt_disable_tls = false,
    };
    strncpy(cfg.uuid,         uuid,         sizeof(cfg.uuid) - 1);
    strncpy(cfg.authkey,      authkey,      sizeof(cfg.authkey) - 1);
    strncpy(cfg.product_key,  product_key,  sizeof(cfg.product_key) - 1);
    if (firmware_key && firmware_key[0])
        strncpy(cfg.firmware_key, firmware_key, sizeof(cfg.firmware_key) - 1);

    printf("[%s] Activating with token: %s\n", TAG, token);
    iot_client_t *client = iot_client_init_on_boarding_with_token(&cfg, token);
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init_on_boarding_with_token failed\n", TAG);
        return -1;
    }

    printf("[%s] Activation successful!\n", TAG);
    printf("[%s] devid      : %s\n", TAG, client->devid);
    printf("[%s] secret_key : %s\n", TAG, client->secret_key);
    printf("[%s] local_key  : %s\n", TAG, client->local_key);

    char session_token[2048] = {0};
    int ret = iot_client_get_session_token(client, NULL, session_token, sizeof(session_token));
    if (ret != OPRT_OK || session_token[0] == '\0') {
        fprintf(stderr, "[%s] iot_client_get_session_token failed: %d\n", TAG, ret);
        iot_client_deinit(client);
        return -1;
    }
    printf("[%s] Session token acquired (len=%zu)\n", TAG, strlen(session_token));

    iot_client_deinit(client);
    return 0;
}
