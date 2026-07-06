#ifndef __IOT_ON_BOARDING_H__
#define __IOT_ON_BOARDING_H__

#include "iot_config_defaults.h"

/** @brief Internal on-boarding configuration (populated by iot_client.c from public config). */
typedef struct {
    char uuid[32];
    char authkey[64];
    char sw_ver[32];
    char product_key[32];
    char pv[16];
    char bv[16];
    const char *modules;
    const char *feature;
    const char *skill_param;
    char firmware_key[64];
    const char *cacert;         // CA cert for all TLS (caller-owned)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
    int timeout_ms;
    iot_env_t env;
    bool mqtt_disable_tls;
    const char *dns_host;       // DNS host override (NULL = default IoT DNS)
    uint16_t dns_port;          // DNS port override (0 = default)
} on_boarding_config_t;

/** @brief On-boarding activation response containing device credentials. */
typedef struct {
    char devid[64];
    char secret_key[64];
    char local_key[64];
    char product_key[64];
    char pv[16];
    char bv[16];
    char uuid[64];
    char schema_id[64];
    char *schema;               // Device schema JSON (caller must free via pal->free)
    iot_region_t region;
    iot_env_t env;
} on_boarding_response_t;

/**
 * @brief Activate device via QR code on-boarding (MQTT-based).
 *
 * Connects to the MQTT broker (resolved via IoT DNS), subscribes to the
 * activation topic, and blocks until the user scans the QR code or the
 * configured timeout expires. On success, sends the ATOP activation request
 * and fills @p response with device credentials.
 *
 * @param pal         PAL adapter
 * @param on_boarding On-boarding configuration (uuid, authkey, timeout_ms, etc.)
 * @param response    Output: activated device credentials (devid, secret_key, local_key)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if on_boarding or response is NULL
 */
int on_boarding_with_qrcode(const pal_t *pal, on_boarding_config_t *on_boarding,
                            on_boarding_response_t *response);

/**
 * @brief Activate device via token on-boarding (direct ATOP call).
 *
 * Parses region from the first 2 characters and secret from the last 4
 * characters of @p token, then sends the ATOP activation request directly.
 * Does not use MQTT or DNS.
 *
 * @param pal         PAL adapter
 * @param on_boarding On-boarding configuration (uuid, authkey, product_key, etc.)
 * @param token       Activation token: [region:2][activation_token][secret:4] (min 7 chars)
 * @param response    Output: activated device credentials
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if any required param is NULL/empty or token too short
 */
int on_boarding_with_token(const pal_t *pal, on_boarding_config_t *on_boarding,
                           const char *token, on_boarding_response_t *response);

#endif /* __IOT_ON_BOARDING_H__ */
