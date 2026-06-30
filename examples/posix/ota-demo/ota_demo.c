/**
 * @file ota_demo.c
 * @brief POSIX OTA demo using the agentic-kit iot_ota API.
 *
 * Demonstrates the cloud side of a firmware OTA upgrade:
 *   1. Initialize iot_client with activated device credentials.
 *   2. Report the current firmware version via iot_ota_report_version().
 *   3. Check the cloud for a firmware upgrade via iot_ota_check_upgrade().
 *   4. If an upgrade is available, report UPGRADING status.
 *   5. If --download is given, fetch the firmware image to a local file
 *      (simulating what a real device would write to flash).
 *   6. Report SUCCESS (or FAILURE).
 *
 * On a real embedded target (see examples/esp-idf/ota-demo), steps 5-6 use
 * the platform's OTA APIs (e.g. esp_ota_write) instead of fwrite.
 */

#include "ota_demo.h"

#include "iot_client.h"
#include "iot_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

/* Minimal HTTP client for downloading the firmware image.
 * The SDK's http_client_interface is designed for small JSON ATOP responses
 * (fixed buffer), so for the firmware binary we use libcurl via popen, or
 * more simply, just report the URL and let the caller decide.
 *
 * For this POSIX demo, we download using the standard C library + OpenSSL
 * is overkill; instead we shell out to curl(1) which is universally available
 * on dev machines. */
#include <sys/wait.h>

#define TAG "ota_demo"

/* ----------------------------------------------------------------------- */
/* Firmware download (simulated — writes to a local file)                  */
/* ----------------------------------------------------------------------- */

static int download_firmware(const char *url, const char *out_path,
                             long expected_size)
{
    printf("[%s] downloading %s -> %s\n", TAG, url, out_path);

    /* Use curl(1) for simplicity in this POSIX demo.
     * A real device uses the platform HTTP client + flash-write API. */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -sSL -o '%s' '%s'", out_path, url);

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "[%s] curl failed (rc=%d)\n", TAG, rc);
        return -1;
    }

    /* Verify the downloaded file size */
    struct stat st;
    if (stat(out_path, &st) != 0) {
        fprintf(stderr, "[%s] cannot stat %s\n", TAG, out_path);
        return -1;
    }

    printf("[%s] downloaded %lld bytes\n", TAG, (long long)st.st_size);

    if (expected_size > 0 && (long)st.st_size != expected_size) {
        fprintf(stderr, "[%s] size mismatch: got %lld, expected %ld\n",
                TAG, (long long)st.st_size, expected_size);
        return -1;
    }

    printf("[%s] firmware image saved to %s\n", TAG, out_path);
    printf("[%s] (on a real device, this would be written to OTA flash)\n", TAG);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Demo entry point                                                        */
/* ----------------------------------------------------------------------- */

int demo_ota_run(const char *devid, const char *secret_key, const char *local_key,
                 const char *sw_ver, int auto_download)
{
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return -1;
    }
    log_set_level(LOG_INFO);

    /* 1. Initialize iot_client */
    iot_client_config_t cfg = {
        .region            = AY,
        .env               = PROD,
        .mqtt_disable_tls  = false,
        .mqtt_auto_connect = false,
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

    /* 2. Report current firmware version */
    printf("[%s] reporting firmware version: %s\n", TAG, sw_ver);
    int rc = iot_ota_report_version(client, sw_ver);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] iot_ota_report_version failed: %d\n", TAG, rc);
        /* non-fatal — continue to upgrade check */
    } else {
        printf("[%s] version reported successfully\n", TAG);
    }

    /* 3. Check for firmware upgrade */
    printf("[%s] checking cloud for firmware upgrade...\n", TAG);
    iot_ota_upgrade_info_t info = {0};
    rc = iot_ota_check_upgrade(client, 0, sw_ver, &info);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] iot_ota_check_upgrade failed: %d\n", TAG, rc);
        iot_ota_upgrade_info_free(client, &info);
        iot_client_deinit(client);
        return -1;
    }

    if (!info.has_upgrade) {
        printf("[%s] no firmware upgrade available\n", TAG);
        iot_ota_upgrade_info_free(client, &info);
        iot_client_deinit(client);
        return 0;
    }

    /* 4. Print upgrade info */
    printf("[%s] ===== firmware upgrade available =====\n", TAG);
    printf("[%s]   version : %s\n", TAG, info.version ? info.version : "?");
    printf("[%s]   url     : %s\n", TAG, info.url);
    printf("[%s]   size    : %ld bytes\n", TAG, info.file_size);
    printf("[%s]   channel : %d\n", TAG, info.channel);
    printf("[%s]   md5     : %s\n", TAG, info.md5 ? info.md5 : "(none)");
    printf("[%s]   hmac    : %s\n", TAG, info.hmac ? info.hmac : "(none)");

    /* 5. Report UPGRADING status */
    printf("[%s] reporting UPGRADING status...\n", TAG);
    rc = iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRADING);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[%s] failed to report UPGRADING: %d\n", TAG, rc);
    }

    /* 6. Optionally download the firmware image */
    int result = 0;
    if (auto_download && info.url) {
        char out_path[256];
        snprintf(out_path, sizeof(out_path), "firmware_%s.bin",
                 info.version ? info.version : "unknown");

        if (download_firmware(info.url, out_path, info.file_size) != 0) {
            fprintf(stderr, "[%s] firmware download failed\n", TAG);
            printf("[%s] reporting FAILURE status...\n", TAG);
            iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRD_EXEC);
            result = -1;
        } else {
            printf("[%s] reporting SUCCESS status...\n", TAG);
            iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRAD_FINI);
        }
    } else {
        printf("[%s] (use --download to fetch the image)\n", TAG);
        printf("[%s] reporting ABORTED status (demo mode)...\n", TAG);
        iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRD_ABORT);
    }

    iot_ota_upgrade_info_free(client, &info);
    iot_client_deinit(client);
    return result;
}
