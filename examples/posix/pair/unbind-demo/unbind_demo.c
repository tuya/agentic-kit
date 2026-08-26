/**
 * @file unbind_demo.c
 * @brief Cloud device-remove (unbind/reset) demo for an activated device.
 *
 * Flow:
 *   1. Initialize iot_client with activated device credentials.
 *   2. Register a reset_callback that sets a flag and prints the type.
 *   3. Connect to MQTT and pump the receive loop.
 *   4. When the cloud pushes protocol 11 (user removes the device from
 *      the app), the callback fires, the flag breaks the loop, and the
 *      demo exits.
 *
 * No storage is wiped here — the demo only demonstrates detection.
 * See dp_management_demo for the full teardown + state-wipe pattern.
 */

#include "unbind_demo.h"

#include "iot_client.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define TAG "unbind_demo"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void on_reset(iot_reset_type_t type, void *user_data)
{
    (void)user_data;
    printf("\n[%s] ** device-remove notice received **\n", TAG);
    printf("[%s]    type : %s\n", TAG,
           type == IOT_RESET_REMOTE_FACTORY ? "factory_reset" : "remote_unbind");
    printf("[%s] The device was removed from the cloud.\n", TAG);
    printf("[%s] On a real device: wipe credentials, clear stored state,\n", TAG);
    printf("[%s] and re-enter pairing mode.\n", TAG);
    g_running = 0;
}

static void on_message(const char *topic, size_t topic_len,
                       const uint8_t *data, size_t data_len)
{
    printf("[%s] message: topic=%.*s (%zu bytes)\n",
           TAG, (int)topic_len, topic, data_len);
}

int demo_unbind_run(const char *devid, const char *secret_key,
                    const char *local_key)
{
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return -1;
    }
    log_set_level(LOG_INFO);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    iot_client_config_t cfg = {
        .region            = AY,
        .env               = PROD,
        .mqtt_disable_tls  = false,
        .mqtt_disable_auto_connect = true,
        .reset_callback    = on_reset,
        .message_callback  = on_message,
    };
    strncpy(cfg.devid,      devid,      sizeof(cfg.devid) - 1);
    strncpy(cfg.secret_key, secret_key, sizeof(cfg.secret_key) - 1);
    strncpy(cfg.local_key,  local_key,  sizeof(cfg.local_key) - 1);

    iot_client_t *client = iot_client_init(&cfg);
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init failed\n", TAG);
        return -1;
    }
    printf("[%s] client initialized (devid=%s)\n", TAG, client->devid);

    int ret = iot_client_connect(client);
    if (ret != OPRT_OK) {
        fprintf(stderr, "[%s] MQTT connect failed: %d\n", TAG, ret);
        iot_client_deinit(client);
        return -1;
    }
    printf("[%s] MQTT connected — waiting for device-remove notice\n", TAG);
    printf("[%s] Remove the device from the app to trigger the callback.\n", TAG);
    printf("[%s] (Ctrl-C to quit without unbinding)\n\n", TAG);

    while (g_running) {
        int rc = iot_client_process(client, 200);
        if (rc != OPRT_OK && g_running) {
            fprintf(stderr, "[%s] link error %d; reconnecting...\n", TAG, rc);
            iot_client_disconnect(client);
            sleep(2);
            ret = iot_client_connect(client);
            if (ret != OPRT_OK) {
                fprintf(stderr, "[%s] reconnect failed: %d\n", TAG, ret);
                continue;
            }
            printf("[%s] reconnected\n", TAG);
        }
    }

    printf("\n[%s] shutting down\n", TAG);
    iot_client_disconnect(client);
    iot_client_deinit(client);
    return 0;
}
