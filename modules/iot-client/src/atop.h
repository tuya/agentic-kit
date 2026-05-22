#ifndef __ATOP_H__
#define __ATOP_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iot_client.h"

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

#endif /* ATOP_H */
