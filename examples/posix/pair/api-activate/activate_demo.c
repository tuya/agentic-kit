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
#include "iot_dp.h"

#include <stdio.h>
#include <string.h>

#define TAG "api_activate"

/* Cloud set one or more DPs on this device. */
static void demo_dp_callback(uint8_t dp_id, const iot_dp_value_t *value, void *user_data)
{
    (void)user_data;
    printf("[%s] DP downlink: id=%u type=%d\n", TAG, dp_id, value->type);
    /* React to the new value here (drive hardware, update UI, ...). */
}

/* DP state changed — persist the snapshot. The SDK does NOT write storage; the
 * application owns that. Each callback carries the full state, so dropping
 * intermediate snapshots (debounce) is safe. */
static void demo_dp_save_callback(const char *dp_state_json, void *user_data)
{
    (void)user_data;
    printf("[%s] persist DP state: %s\n", TAG, dp_state_json);
    /* e.g. write dp_state_json to a file / flash / NVS keyed by devid. */
}

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

    /* ---- Minimal DP layer usage ----
     * The DP registry was built from the activation schema automatically.
     * Persist client->schema_id / client->schema (and the dp_state snapshots
     * below) so a later boot can restore via iot_client_config_t. */
    printf("[%s] schema_id  : %s\n", TAG, client->schema_id);
    iot_dp_set_callback(client, demo_dp_callback, NULL);
    iot_dp_set_save_callback(client, demo_dp_save_callback, NULL);

    /* Update a couple of DPs locally, then batch-report the dirty ones. Adjust
     * the ids/types to match your product schema. */
    iot_dp_value_t on = { .type = IOT_DP_TYPE_BOOL, .value.boolean = true };
    if (iot_dp_set(client, 1, &on) == OPRT_OK) {
        iot_dp_report_all_dirty(client);
    }

    /* Pull the current state on demand (alternative to the save callback). */
    char *dp_state = NULL;
    if (iot_dp_dump_json(client, &dp_state) == OPRT_OK && dp_state) {
        printf("[%s] DP state snapshot: %s\n", TAG, dp_state);
        client->pal->free(dp_state);
    }

    /* Receive downlinks: call iot_client_process(client, timeout) in your loop. */

    iot_client_deinit(client);
    return 0;
}
