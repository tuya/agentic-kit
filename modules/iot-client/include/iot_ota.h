#ifndef _IOT_OTA_H_
#define _IOT_OTA_H_

#include "iot_client.h"

/**
 * @file iot_ota.h
 * @brief Public OTA (firmware upgrade) API for the agentic-kit IoT client.
 *
 * This module provides cloud-protocol primitives for OTA:
 * - Reporting the device's current firmware version to the cloud.
 * - Checking for an available firmware upgrade.
 * - Reporting the upgrade lifecycle status (upgrading / success / failure).
 *
 * The SDK does **not** download or flash firmware. The application owns the
 * download and flash logic (e.g. ESP-IDF `esp_ota_*` or a vendor-specific
 * bootloader API).
 *
 * Typical flow:
 * ```c
 * // 1. Report current version (also done automatically in iot_client_init)
 * iot_ota_report_version(client, "1.2.3");
 *
 * // 2. Check for an upgrade
 * iot_ota_upgrade_info_t info;
 * if (iot_ota_check_upgrade(client, 0, &info) == OPRT_OK && info.has_upgrade) {
 *     // 3. Signal start of upgrade
 *     iot_ota_report_status(client, 0, OTA_STATUS_UPGRADING);
 *     // 4. Download info.url and flash (application code)
 *     my_firmware_download_and_flash(info.url);
 *     // 5. After reboot, report success — or report failure before retry
 * }
 * iot_ota_upgrade_info_free(&info);
 * ```
 */

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define OTA_API __attribute__((visibility("default")))
#else
#define OTA_API
#endif

/* ---- Re-export common types from atop.h for public consumers ---- */

/**
 * @brief OTA upgrade status codes.
 *
 * These map to the Tuya cloud's upgrade status values:
 * - 0: idle / pending
 * - 1: upgrade in progress
 * - 2: upgrade finished successfully
 * - 3: upgrade failed / abnormal
 * - 4: upgrade aborted
 */
typedef enum {
    OTA_STATUS_IDLE        = 0,
    OTA_STATUS_UPGRADING   = 1,
    OTA_STATUS_UPGRAD_FINI = 2,
    OTA_STATUS_UPGRD_EXEC  = 3,
    OTA_STATUS_UPGRD_ABORT = 4,
} iot_ota_status_t;

/**
 * @brief Firmware upgrade information returned by iot_ota_check_upgrade().
 *
 * String fields are heap-allocated; free with iot_ota_upgrade_info_free().
 */
typedef struct {
    bool has_upgrade;   /**< true if the cloud returned firmware info */
    char *version;      /**< new firmware version string (NULL if none) */
    char *url;          /**< download URL (httpsUrl) */
    long  file_size;    /**< firmware file size in bytes */
    int   channel;      /**< firmware type returned by cloud */
    char *md5;          /**< MD5 hash of firmware (may be NULL) */
    char *hmac;         /**< HMAC of firmware (may be NULL) */
} iot_ota_upgrade_info_t;

/**
 * @brief Report the device's current firmware version to the cloud.
 *
 * Sends the firmware version via the `tuya.device.versions.update` ATOP API.
 * This is automatically called during iot_client_init() with the SDK default
 * version; call this explicitly to report an application-specific version.
 *
 * @param client  IoT client instance (must be initialized)
 * @param sw_ver  Firmware version string (e.g. "1.2.3"); must not be NULL
 * @return OPRT_OK on success, error code on failure
 */
OTA_API int iot_ota_report_version(iot_client_t *client, const char *sw_ver);

/**
 * @brief Check for an available firmware upgrade.
 *
 * Sends the `tuya.device.upgrade.get` ATOP API. If the cloud has a pending
 * upgrade, @p info is populated with the firmware URL, version, size, and
 * hashes.
 *
 * @param[in]  client   IoT client instance (must be initialized)
 * @param[in]  channel  Firmware channel (0 = main MCU firmware)
 * @param[out] info     Output: upgrade info (caller must free with
 *                      iot_ota_upgrade_info_free())
 * @return OPRT_OK on success (including no-upgrade case), error code on failure
 */
OTA_API int iot_ota_check_upgrade(iot_client_t *client, int channel,
                                  iot_ota_upgrade_info_t *info);

/**
 * @brief Report the OTA upgrade status to the cloud.
 *
 * Sends the `tuya.device.upgrade.status.update` ATOP API. Call this to notify
 * the cloud of the upgrade lifecycle:
 * - OTA_STATUS_UPGRADING before starting the download/flash.
 * - OTA_STATUS_UPGRAD_FINI after a successful upgrade (typically after reboot).
 * - OTA_STATUS_UPGRD_EXEC on failure.
 *
 * @param client   IoT client instance (must be initialized)
 * @param channel  Firmware channel (0 = main MCU firmware)
 * @param status   Upgrade status to report
 * @return OPRT_OK on success, error code on failure
 */
OTA_API int iot_ota_report_status(iot_client_t *client, int channel,
                                  iot_ota_status_t status);

/**
 * @brief Free memory in an iot_ota_upgrade_info_t.
 *
 * Safe to call on a zeroed struct or after a no-upgrade result.
 *
 * @param client  IoT client instance (provides the PAL allocator)
 * @param info    Upgrade info to free (fields zeroed after free)
 */
OTA_API void iot_ota_upgrade_info_free(iot_client_t *client, iot_ota_upgrade_info_t *info);

#endif /* _IOT_OTA_H_ */
