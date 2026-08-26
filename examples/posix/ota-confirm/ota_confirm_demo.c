/**
 * @file ota_confirm_demo.c
 * @brief APP-confirmed OTA demo for an already-activated POSIX/macOS device.
 *
 * Flow:
 *   1. Initialize iot_client with the activated device credentials.
 *   2. Register ota_confirm_callback. The callback only records the channel;
 *      it never performs an ATOP call, downloads firmware, or writes flash.
 *   3. Connect to MQTT and wait for the app user to confirm the OTA task.
 *   4. After the MQTT receive loop signals the main thread, call
 *      iot_ota_check_upgrade() from this main thread.
 *   5. With --download, fetch and verify the image locally and report status.
 */

#include "ota_demo.h"

#include "iot_client.h"
#include "iot_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define TAG "ota_confirm_demo"

typedef struct {
    volatile sig_atomic_t received;
    int channel;
} ota_confirm_state_t;

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void on_ota_confirm(int channel, void *user_data)
{
    ota_confirm_state_t *state = user_data;
    state->received = 1;
    state->channel = channel;
}

static void on_message(const char *topic, size_t topic_len,
                       const uint8_t *data, size_t data_len)
{
    printf("[%s] message: topic=%.*s (%zu bytes)\n",
           TAG, (int)topic_len, topic, data_len);
}

static int reconnect_with_backoff(iot_client_t *client)
{
    static const unsigned int delays[] = {1, 2, 4, 8, 8};

    for (int attempt = 1; attempt <= 5 && g_running; attempt++) {
        printf("[%s] reconnecting (attempt %d/5)...\n", TAG, attempt);
        if (iot_client_connect(client) == OPRT_OK) {
            printf("[%s] reconnected\n", TAG);
            return 0;
        }
        sleep(delays[attempt - 1]);
    }
    return -1;
}

static int run_confirmed_upgrade(iot_client_t *client, int channel,
                                 int download)
{
    iot_ota_upgrade_info_t info = {0};

    printf("[%s] application worker: checking channel %d via upgrade.get\n",
           TAG, channel);
    int rc = iot_ota_check_upgrade(client, channel, &info);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] iot_ota_check_upgrade failed: %d\n", TAG, rc);
        return -1;
    }

    if (!info.has_upgrade) {
        printf("[%s] cloud returned no pending firmware for channel %d\n",
               TAG, channel);
        iot_ota_upgrade_info_free(client, &info);
        return 0;
    }

    printf("[%s] ===== confirmed firmware available =====\n", TAG);
    printf("[%s]   version : %s\n", TAG, info.version ? info.version : "?");
    printf("[%s]   url     : %s\n", TAG, info.url ? info.url : "?");
    printf("[%s]   size    : %ld bytes\n", TAG, info.file_size);
    printf("[%s]   channel : %d\n", TAG, info.channel);
    printf("[%s]   md5     : %s\n", TAG, info.md5 ? info.md5 : "(none)");
    printf("[%s]   hmac    : %s\n", TAG, info.hmac ? info.hmac : "(none)");

    int result = 0;
    if (download && info.url && info.url[0] != '\0') {
        printf("[%s] reporting UPGRADING status...\n", TAG);
        rc = iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRADING);
        if (rc != OPRT_OK) {
            fprintf(stderr, "[%s] failed to report UPGRADING: %d\n", TAG, rc);
        }

        char out_path[256];
        snprintf(out_path, sizeof(out_path), "firmware_%s.bin",
                 info.version ? info.version : "unknown");

        if (ota_demo_download_firmware(info.url, out_path, info.file_size) != 0 ||
            ota_demo_verify_firmware(client, out_path, &info) != 0) {
            fprintf(stderr, "[%s] firmware download/verify failed\n", TAG);
            printf("[%s] reporting ERROR status...\n", TAG);
            iot_ota_report_status(client, info.channel, OTA_STATUS_ERROR);
            result = -1;
        } else {
            printf("[%s] reporting COMPLETE status...\n", TAG);
            rc = iot_ota_report_status(client, info.channel, OTA_STATUS_COMPLETE);
            if (rc != OPRT_OK) {
                fprintf(stderr, "[%s] failed to report COMPLETE: %d\n", TAG, rc);
            }
        }
    } else {
        printf("[%s] query-only mode: no status reported\n", TAG);
    }

    iot_ota_upgrade_info_free(client, &info);
    return result;
}

int demo_ota_confirm_run(const char *devid, const char *secret_key,
                         const char *local_key, const char *region_name,
                         int download)
{
    iot_region_t region = AY;
    if (strcmp(region_name, "AZ") == 0) {
        region = AZ;
    } else if (strcmp(region_name, "UEAZ") == 0) {
        region = UEAZ;
    } else if (strcmp(region_name, "EU") == 0) {
        region = EU;
    } else if (strcmp(region_name, "WEAZ") == 0) {
        region = WEAZ;
    } else if (strcmp(region_name, "IN") == 0) {
        region = IN;
    } else if (strcmp(region_name, "SG") == 0) {
        region = SG;
    } else if (strcmp(region_name, "AY") != 0) {
        fprintf(stderr, "[%s] unsupported region '%s'\n", TAG, region_name);
        return 1;
    }

    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return 1;
    }
    log_set_level(LOG_INFO);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    ota_confirm_state_t state = {0};

    iot_client_config_t cfg = {
        .region               = region,
        .env                  = PROD,
        .mqtt_disable_tls     = false,
        .mqtt_disable_auto_connect = true,
        .message_callback     = on_message,
        .ota_confirm_callback = on_ota_confirm,
        .ota_confirm_user_data = &state,
        .sw_ver               = "1.0.0",
    };
    strncpy(cfg.devid, devid, sizeof(cfg.devid) - 1);
    strncpy(cfg.secret_key, secret_key, sizeof(cfg.secret_key) - 1);
    strncpy(cfg.local_key, local_key, sizeof(cfg.local_key) - 1);

    iot_client_t *client = iot_client_init(&cfg);
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init failed\n", TAG);
        return 1;
    }

    printf("[%s] client initialized (devid=%s, region=%s)\n",
           TAG, client->devid, region_name);
    int rc = iot_client_connect(client);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] MQTT connect failed: %d\n", TAG, rc);
        iot_client_deinit(client);
        return 1;
    }

    printf("[%s] MQTT connected; waiting for APP OTA confirmation\n", TAG);
    printf("[%s] confirm the upgrade in the app (Ctrl-C cancels)\n\n", TAG);

    int result = 0;
    while (g_running && !state.received) {
        rc = iot_client_process(client, 200);
        if (rc != OPRT_OK) {
            fprintf(stderr, "[%s] MQTT process failed: %d\n", TAG, rc);
            iot_client_disconnect(client);
            if (reconnect_with_backoff(client) != 0) {
                fprintf(stderr, "[%s] give up after reconnect failures\n", TAG);
                result = 1;
                goto out;
            }
        }
    }

    if (state.received) {
        printf("\n[%s] ** APP-confirmed OTA notice received (channel=%d) **\n",
               TAG, state.channel);
        if (run_confirmed_upgrade(client, state.channel, download) != 0) {
            result = 1;
        }
    } else {
        printf("\n[%s] cancelled before APP confirmation\n", TAG);
    }

out:
    iot_client_disconnect(client);
    iot_client_deinit(client);
    return result;
}
