#ifndef _IOT_CLIENT_H_
#define _IOT_CLIENT_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pal.h"
#include "log.h"
#include "tls.h"

#if defined(__GNUC__) && (__GNUC__ >= 4)
#define IOT_API __attribute__((visibility("default")))
#else
#define IOT_API
#endif

/* ---- Error codes ---- */

#define OPRT_OK                       (0x0000)  // 0, Execution successful
#define OPRT_COMMUNICATION_ERROR      (-0x0001) //-1, Communication error
#define OPRT_INVALID_PARAMETER        (-0x0002) //-2, Invalid parameter
#define OPRT_INVALID_RESULT           (-0x0003) //-3, Invalid result
#define OPRT_UNINITIALIZED            (-0x0004) //-4, Uninitialized
#define OPRT_NOT_SUPPORTED            (-0x0005) //-5, Not supported
#define OPRT_MALLOC_FAILED            (-0x0006) //-6, Memory allocation failed
#define OPRT_TLS_HANDSHAKE_FAILED     (-0x0007) //-7, TLS handshake failed

/* ---- Logging subsystem ----
 * The IoT SDK shares the process-wide log facade (see log.h).
 * To redirect output: log_set_handler(my_fn);
 * To filter at runtime: log_set_level(LOG_INFO);
 */

typedef enum {
    AY = 0,
    US,
    UEAZ,
    EU,
    WEAZ,
    IN,
    SG
} iot_region_t;

typedef enum {
    PROD = 0,
    PRE,
    TEST,
} iot_env_t;

/**
 * @brief Initialize IoT SDK with the built-in default PAL adapter (POSIX / FreeRTOS).
 *
 * Must be called before any other SDK function.  Logging is dispatched
 * through the log facade — install a custom handler with
 * log_set_handler() if you need non-default output.
 *
 * @return OPRT_OK on success
 */
IOT_API int iot_init_default(void);

/**
 * @brief Initialize IoT SDK with a custom PAL adapter.
 *
 * Must be called before any other SDK function. The PAL struct must not be
 * NULL and all required function pointers must be set; otherwise returns
 * OPRT_INVALID_PARAMETER.
 *
 * @param[in] pal  PAL adapter (non-NULL, all required function pointers required)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if pal is NULL or incomplete
 */
IOT_API int iot_init(const pal_t *pal);

/**
 * @brief Callback for incoming MQTT messages (already decrypted).
 *
 * @param topic     MQTT topic string.
 * @param topic_len Length of topic.
 * @param data      Decrypted payload bytes.
 * @param data_len  Length of decrypted payload.
 */
typedef void (*iot_message_callback_t)(const char *topic, size_t topic_len,
                                       const uint8_t *data, size_t data_len);

/**
 * @brief IoT client configuration structure
 */
typedef struct {
    char devid[32];                // Device ID
    char secret_key[32];           // Secret key
    char local_key[32];            // Local key
    iot_region_t region;           // Region
    iot_env_t env;                 // Environment
    bool mqtt_disable_tls;         // false = mqtts (TLS, default), true = mqtt (TCP)
    bool mqtt_auto_connect;        // true = connect MQTT after init/activation; false (default) = caller invokes iot_client_message_connect() manually
    const char *cacert;            // CA cert for all TLS (MQTT/HTTPS/IoT-DNS) (PEM, caller-owned, must outlive client)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
    iot_message_callback_t message_callback; // MQTT message callback

    /* ---- DP layer restore (all caller-owned, may be NULL) ---- */
    const char *schema;            // Persisted DP schema JSON to restore on restart (NULL = none / loose mode)
    const char *schema_id;         // Persisted schema id (stable key for schema upgrade query)
    const char *dp_state;          // Persisted DP current state {"dps":{...}} to restore (no dirty, no report)
    const char *sw_ver;            // Application firmware version (e.g. "1.2.3"); NULL = use SDK default IOT_SDK_SW_VER
} iot_client_config_t;

/**
 * @brief IoT on boarding configuration structure
 */
typedef struct {
    char uuid[32];
    char authkey[64];
    char product_key[32];
    char firmware_key[64];
    const char *modules;
    const char *feature;
    const char *skill_param;
    int timeout_ms;
    iot_env_t env;                 // PROD (default) or PRE
    bool mqtt_disable_tls;         // false = mqtts (TLS, default), true = mqtt (TCP)
    bool mqtt_auto_connect;        // true = connect MQTT after init/activation; false (default) = caller invokes iot_client_message_connect() manually
    const char *cacert;            // CA cert for all TLS (MQTT/HTTPS/IoT-DNS) (PEM, caller-owned, must outlive client)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
    iot_message_callback_t message_callback; // MQTT message callback
    const char *sw_ver;            // Application firmware version (e.g. "1.2.3"); NULL = use SDK default IOT_SDK_SW_VER
} iot_on_boarding_config_t;


/* Opaque DP-layer context; defined privately in src/iot_dp.c. Its storage is
 * inlined into iot_client_t (dp_storage below) to avoid a per-session heap
 * allocation; iot_dp.c _Static_asserts that the real struct fits. */
struct iot_dp_context;
#define IOT_DP_CONTEXT_STORAGE 128

/**
 * @brief IoT client instance structure
 *
 * This structure holds the state and configuration of an IoT client connection.
 * Created by iot_client_init() and destroyed by iot_client_deinit().
 */
 typedef struct {
    char devid[32];                // Device ID assigned after activation
    char secret_key[32];           // Secret key for MQTT authentication
    char local_key[32];            // Local encryption key for LAN communication
    char schema_id[64];            // Device schema ID (from activation; stable key for schema upgrade)

    char https_url[64];           // HTTPS endpoint URL for ATOP (inline; "" = unresolved)
    char mqtt_url[64];            // MQTT broker URL (inline, mqtt://|mqtts://; "" = unresolved)
    char *schema;                 // Device schema JSON (dynamically allocated)

    iot_region_t region;           // Server region (CN, US, EU, IN)
    iot_env_t env;                 // Environment (PROD or PRE)
    bool mqtt_disable_tls;         // false = mqtts (TLS), true = mqtt (TCP)
    const pal_t *pal;             // PAL adapter

    const char *cacert;           // CA certificate for all TLS (MQTT/HTTPS/IoT-DNS) (caller-owned, points to user buffer/flash)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (borrowed, NULL = none)
    struct mqtt_client *mqtt;     // Internal MQTT client handle
    iot_message_callback_t message_callback;  // User callback for incoming messages

    struct iot_dp_context *dp;    // DP layer state; points into dp_storage, NULL when inactive
    void *dp_storage[IOT_DP_CONTEXT_STORAGE / sizeof(void *)]; // inline storage for *dp (no heap)
 } iot_client_t;

/**
 * @brief Initialize IoT client with existing device credentials.
 *
 * Resolves MQTT/HTTPS endpoints via IoT DNS and establishes the MQTT
 * connection automatically when devid is set.
 *
 * @param config Client configuration (devid, secret_key, local_key, region, etc.)
 * @return Pointer to iot_client_t on success, NULL on error
 */
IOT_API iot_client_t *iot_client_init(const iot_client_config_t *config);

/**
 * @brief Initialize IoT client via QR code on-boarding (first-time activation).
 *
 * Blocks until a user scans the QR code and the device is activated, or
 * until the configured timeout expires.  On success the returned client is
 * fully connected; persist its devid / secret_key / local_key for future
 * calls to iot_client_init().
 *
 * @param config On-boarding configuration (uuid, authkey, product_key, timeout_ms, etc.)
 * @return Pointer to iot_client_t on success (contains devid, secret_key, local_key, schema_id), NULL on error or timeout
 */
IOT_API iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);

/**
 * @brief Initialize IoT client via token on-boarding (first-time activation).
 *
 * Skips the QR-code MQTT activation wait and directly sends the activation
 * request using the provided token. Region is derived from the first two
 * characters of the token; env is taken from @p config.
 * Does not require DNS/MQTT — calls ATOP directly.
 *
 * @param config On-boarding configuration (uuid, authkey, product_key, etc.)
 * @param token  Activation token: [region:2][activation_token][secret:4]
 * @return Pointer to iot_client_t on success, NULL on error
 */
IOT_API iot_client_t *iot_client_init_on_boarding_with_token(const iot_on_boarding_config_t *config, const char *token);

/**
 * @brief Destroy IoT client and release all resources.
 *
 * Disconnects MQTT, frees URLs, certificates, schema, and the client struct.
 * Safe to call with NULL (no-op).
 *
 * @param client Pointer to iot_client_t instance (NULL is safe)
 */
IOT_API void iot_client_deinit(iot_client_t *client);

/**
 * @brief Process MQTT events (call this in a loop to receive messages)
 * @param client Pointer to iot_client_t instance
 * @param timeout_ms Processing timeout in milliseconds
 * @return OPRT_OK on success, error code on failure
 */
IOT_API int iot_client_process(iot_client_t *client, uint32_t timeout_ms);

/**
 * @brief Publish an encrypted message via MQTT.
 *
 * Encrypts @p data with AES-128-GCM (P2.3) using the client's local_key
 * and publishes to the device's outbound topic.
 *
 * @param client   Pointer to iot_client_t instance
 * @param data     Plaintext data to encrypt and send
 * @param data_len Length of data in bytes
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client is NULL
 */
IOT_API int iot_client_publish(iot_client_t *client, const uint8_t *data, size_t data_len);

/**
 * @brief Get AI agent session token from Tuya cloud.
 *
 * On TLS handshake failure, automatically refreshes the HTTPS CA certificate
 * and retries once.
 *
 * @param client     Pointer to iot_client_t instance
 * @param agent_code Agent code string (NULL for default)
 * @param token      Output buffer for the token
 * @param token_len  Size of the output buffer in bytes
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client or token is NULL
 */
IOT_API int iot_client_get_session_token(iot_client_t *client, const char *agent_code, char *token, size_t token_len);

/**
 * @brief Get CA certificate for a target host via IoT DNS service.
 *
 * @param client         Pointer to iot_client_t instance (must not be NULL)
 * @param host           Target host to get certificate for
 * @param port           Target port
 * @param ca_certificate Output CA certificate PEM string (caller must free via pal->free)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client or host is NULL
 */
IOT_API int iot_get_ca_certificate(iot_client_t *client, const char *host, uint16_t port, char **ca_certificate);

/**
 * @brief QR code info request parameters
 */
typedef struct {
    const char *uuid;         // Device UUID (used for signing)
    const char *authkey;      // Auth key (used for signing)
    const char *app_id;       // App ID (empty string if not specified)
    int type;                 // QR code type
    iot_region_t region;      // Region (AY = China, default)
    iot_env_t env;            // Environment (PROD or PRE)
    const char *cacert;       // CA cert for HTTPS/IoT-DNS TLS (PEM, caller-owned)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
} iot_qrcode_request_t;

/**
 * @brief QR code info response
 */
typedef struct {
    char *url;                // QR code URL (caller must free)
} iot_qrcode_response_t;

/**
 * @brief Get QR code info from Tuya cloud.
 *
 * Resolves the ATOP endpoint and CA certificate via IoT DNS automatically —
 * does not require an initialized client.  The caller must free response->url.
 *
 * @param[in]  request  Request parameters (uuid, authkey, app_id, type, region, env)
 * @param[out] response Response containing QR code URL (caller must free url)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if request or response is NULL
 */
IOT_API int iot_get_qrcode_info(const iot_qrcode_request_t *request, iot_qrcode_response_t *response);

#endif /* _IOT_CLIENT_H_ */
