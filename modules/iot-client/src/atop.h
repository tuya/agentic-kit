#ifndef __ATOP_H__
#define __ATOP_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iot_client.h"
#include "iot_ota.h"   /* iot_ota_status_t (shared between atop and public OTA API) */
#include "tls.h"

/**
 * @brief Device activation request structure
 */
 typedef struct {
    const char *token;        // Token
    const char *sw_ver;       // Software version
    const char *product_key;  // Product key
    const char *pv;           // Protocol version
    const char *bv;           // Baseline version
    const char *authkey;      // Auth key
    const char *uuid;         // UUID
    const char *devid;        // Device ID (optional, for activated devices)
    const char *modules;      // Modules (optional)
    const char *feature;      // Feature (optional)
    const char *skill_param;  // Skill parameter (optional)
    const char *sdk_version;  // SDK full version (optional)
    const char *firmware_key; // Firmware key (optional)
    const void *user_data;    // User data (optional)
    const char *host;         // Server host (optional, defaults to TUYA_DEFAULT_HOST)
    uint16_t port;            // Server port (optional, defaults to TUYA_DEFAULT_PORT)
    const char *cacert;       // CA certificate PEM content
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
} activite_request_t;

typedef struct   {
    const char *devid;        // Device ID
    const char *secret_key;   // Secret key
    const char *local_key;     // Local key
    const char *product_key;  // Product key
    const char *pv;           // Protocol version
    const char *bv;           // Baseline version
    const char *uuid;         // UUID
    const char *schema_id;    // Schema ID
    const char *schema;       // Schema
} activite_response_t;
/**
 * @brief Sends an activate request to the ATOP service.
 *
 * This function sends an activate request to the ATOP service using the
 * provided request parameters.
 *
 * @param request The activate request parameters.
 * @param response The response structure to store the result.
 * @return Returns OPRT_OK if the request is successful, otherwise returns an
 * error code.
 */
int atop_activate_request(const pal_t *pal, const activite_request_t *request, activite_response_t *response);

/**
 * @brief Free memory allocated in activate response
 * @param response The response structure to free
 */
void atop_activate_response_free(const pal_t *pal, activite_response_t *response);

/**
 * @brief AI token response structure
 */
typedef struct {
    char *token;                     // JSON string (caller must free)
} ai_token_response_t;

/**
 * @brief Request parameters for getting AI token
 */
typedef struct {
    const char *devid;               // Device ID
    const char *key;                 // Device secret key
    const char *agent_code;          // Agent code
    const char *host;                // Server host (optional, defaults to TUYA_DEFAULT_HOST)
    uint16_t port;                   // Server port (optional, defaults to TUYA_DEFAULT_PORT)
    const char *cacert;              // CA certificate PEM content
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
} ai_token_request_t;

/**
 * @brief Gets AI configuration information from Tuya cloud.
 *
 * This function sends a request to get AI configuration information including
 * server hosts, ports, credentials, and other connection parameters.
 *
 * @param request The request parameters (devid, key, agent_code).
 * @param response Output structure to store the token (caller must free token with pal->free()).
 * @return Returns OPRT_OK if the request is successful, otherwise returns an error code.
 */
int atop_ai_token_get(const pal_t *pal, const ai_token_request_t *request, ai_token_response_t *response);

/**
 * @brief QR code info request parameters
 */
typedef struct {
    const char *uuid;         // Device UUID (used for signing)
    const char *authkey;      // Auth key (used for signing)
    const char *app_id;       // App ID (empty string if not specified)
    int type;                 // QR code type
    const char *host;         // Server host
    uint16_t port;            // Server port
    const char *cacert;       // CA certificate PEM content
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
} qrcode_info_request_t;

/**
 * @brief QR code info response
 */
typedef struct {
    char *short_url;                // QR code URL (caller must free)
} qrcode_info_response_t;

/**
 * @brief Device meta save request parameters
 */
typedef struct {
    const char *devid;         // Device ID
    const char *key;           // Device secret key
    const char *sdk_version;   // SDK version string (e.g. "agentic-kit 0.1")
    const char *host;          // Server host (optional, defaults to TUYA_DEFAULT_HOST)
    uint16_t port;             // Server port (optional, defaults to TUYA_DEFAULT_PORT)
    const char *cacert;        // CA certificate PEM content
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
} device_meta_save_request_t;

/**
 * @brief Device meta save response
 */
typedef struct {
    bool success;              // Whether the save operation succeeded
} device_meta_save_response_t;

/**
 * @brief Save device metadata to Tuya cloud (tuya.device.meta.save)
 *
 * Saves smain_network_sdk_full_version to the device metadata.
 *
 * @param[in]  request  Request parameters (devid, key, sdk_version)
 * @param[out] response Response indicating success
 * @return OPRT_OK on success, error code on failure
 */
int atop_device_meta_save(const pal_t *pal, const device_meta_save_request_t *request, device_meta_save_response_t *response);

/**
 * @brief Get QR code info from Tuya cloud (tuya.device.qrcode.info.get)
 *
 * @param[in]  request  Request parameters (uuid, authkey, app_id, type)
 * @param[out] response Response containing QR code URL (caller must free url)
 * @return OPRT_OK on success, error code on failure
 */
int atop_qrcode_info_get(const pal_t *pal, const qrcode_info_request_t *request, qrcode_info_response_t *response);

/**
 * @brief Newest-schema query request parameters (tuya.device.schema.newest.get).
 */
typedef struct {
    const char *devid;        // Device ID
    const char *key;          // Device secret key (for signing)
    const char *schema_id;    // Stable schema id to query
    const char *version;      // Current local schema version ("" / NULL to always fetch newest)
    const char *node_id;      // Optional sub-device node id (NULL/"" to omit)
    const char *host;         // Server host (optional, defaults to TUYA_DEFAULT_HOST)
    uint16_t port;            // Server port (optional, defaults to TUYA_DEFAULT_PORT)
    const char *cacert;       // CA certificate PEM content
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
} schema_newest_request_t;

/**
 * @brief Newest-schema query response.
 */
typedef struct {
    char *schema;             // New schema JSON (caller frees via pal->free); NULL when no update
    bool updated;             // true if a newer schema was returned
} schema_newest_response_t;

/**
 * @brief Query the newest device schema (tuya.device.schema.newest.get v1.0).
 *
 * @param[in]  request  Request parameters (devid, key, schema_id, version)
 * @param[out] response Response; .updated=false (and .schema=NULL) when cloud
 *                      reports no newer schema (empty / []).
 * @return OPRT_OK on success (including the no-update case), error code on failure.
 */
int atop_schema_newest_get(const pal_t *pal, const schema_newest_request_t *request, schema_newest_response_t *response);

/**
 * @brief Free memory allocated in a schema_newest_response_t.
 */
void atop_schema_newest_response_free(const pal_t *pal, schema_newest_response_t *response);

/* ============================================================================
 * OTA (firmware upgrade) Service
 * ============================================================================ */

/**
 * @brief Request parameters for checking a firmware upgrade
 *        (tuya.device.upgrade.get v4.4).
 */
typedef struct {
    const char *devid;
    const char *key;
    int channel;        /**< firmware type/channel (0 = main MCU) */
    const char *host;
    uint16_t port;
    const char *cacert;
    tls_cert_bundle_attach_fn cert_bundle_attach;
} ota_upgrade_request_t;

/**
 * @brief Response from a firmware upgrade check.
 *
 * Strings are heap-allocated; free with atop_upgrade_get_response_free().
 */
typedef struct {
    bool has_upgrade;   /**< true if the cloud returned firmware info */
    char *version;      /**< new firmware version string (NULL if none) */
    char *url;          /**< download URL (httpsUrl) */
    long  file_size;    /**< firmware file size in bytes */
    int   channel;      /**< firmware type returned by cloud */
    char *md5;          /**< MD5 hash of firmware (may be NULL) */
    char *hmac;         /**< HMAC of firmware (may be NULL) */
} ota_upgrade_response_t;

/**
 * @brief Request parameters for reporting firmware version
 *        (tuya.device.versions.update v4.1).
 */
typedef struct {
    const char *devid;
    const char *key;
    const char *sw_ver;   /**< firmware version string */
    const char *pv;       /**< protocol version (e.g. "2.3") */
    const char *bv;       /**< baseline version (e.g. "2.0") */
    int channel;          /**< firmware channel (0 = main) */
    const char *host;
    uint16_t port;
    const char *cacert;
    tls_cert_bundle_attach_fn cert_bundle_attach;
} ota_version_update_request_t;

/**
 * @brief Request parameters for reporting upgrade status
 *        (tuya.device.upgrade.status.update v4.1).
 */
typedef struct {
    const char *devid;
    const char *key;
    int channel;
    iot_ota_status_t status;
    const char *host;
    uint16_t port;
    const char *cacert;
    tls_cert_bundle_attach_fn cert_bundle_attach;
} ota_status_update_request_t;

/**
 * @brief Check for a firmware upgrade (tuya.device.upgrade.get v4.4).
 *
 * @param[in]  pal      PAL adapter
 * @param[in]  request  Request parameters (devid, key, channel)
 * @param[out] response Response with firmware info (caller frees)
 * @return OPRT_OK on success (including no-upgrade), error code on failure
 */
int atop_upgrade_get(const pal_t *pal, const ota_upgrade_request_t *request, ota_upgrade_response_t *response);

/**
 * @brief Free memory in an ota_upgrade_response_t.
 */
void atop_upgrade_get_response_free(const pal_t *pal, ota_upgrade_response_t *response);

/**
 * @brief Report device firmware version (tuya.device.versions.update v4.1).
 *
 * @param pal     PAL adapter
 * @param request Request parameters (devid, key, sw_ver, pv, bv, channel)
 * @return OPRT_OK on success, error code on failure
 */
int atop_version_update(const pal_t *pal, const ota_version_update_request_t *request);

/**
 * @brief Report upgrade status (tuya.device.upgrade.status.update v4.1).
 *
 * @param pal     PAL adapter
 * @param request Request parameters (devid, key, channel, status)
 * @return OPRT_OK on success, error code on failure
 */
int atop_upgrade_status_update(const pal_t *pal, const ota_status_update_request_t *request);

#endif /* ATOP_H */
