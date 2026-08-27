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

/* -0x0008..-0x000C are used by iot_dp.h (DP error codes) */
#define OPRT_OTA_VERIFY_FAILED        (-0x000D) //-13, OTA firmware digest mismatch
#define OPRT_ATOP_BUSINESS_ERROR      (-0x000E) //-14, ATOP call reached the cloud but was rejected (see errorCode)

/* ---- Logging subsystem ----
 * The IoT SDK shares the process-wide log facade (see log.h).
 * To redirect output: log_set_handler(my_fn);
 * To filter at runtime: log_set_level(LOG_INFO);
 */

typedef enum {
    AY = 0,
    AZ,
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
 * @brief Reset type classification (mirrors TuyaOpen TUYA_RESET_TYPE_REMOTE_*).
 *
 * Inbound only: how to read a device-remove the cloud pushed at us. The
 * outbound choice -- what to ask the cloud for -- is iot_reset_scope_t.
 */
typedef enum {
    IOT_RESET_REMOTE_UNBIND = 0,  // user removed the device (re-bind allowed)
    IOT_RESET_REMOTE_FACTORY,     // cloud-ordered factory reset
} iot_reset_type_t;

/**
 * @brief How much to clear when this device resets itself (iot_client_reset).
 *
 * The outbound counterpart of iot_reset_type_t, and the same two meanings, but
 * kept a separate type on purpose: those constants are named REMOTE_ because
 * they describe a push the cloud initiated, which reads backwards on a call the
 * device makes.
 *
 * An enum rather than a bool because the two differ in whether they can be
 * undone, and a bare `true` at the call site would not say which one it is.
 */
typedef enum {
    /* Drop the user-device binding only. The device's cloud-side data is kept,
     * so re-pairing can pick it up again. Maps to resetFactory=false. */
    IOT_RESET_UNBIND_ONLY = 0,
    /* Factory reset: the cloud additionally discards the data it holds for this
     * device (business-specific exclusions aside). NOT reversible -- re-pairing
     * yields a new binding, not the old state. Maps to resetFactory=true. */
    IOT_RESET_FACTORY,
} iot_reset_scope_t;

/**
 * @brief Callback fired when the cloud pushes a device-remove (protocol 11)
 * notice over MQTT.
 *
 * Fired on the iot_client_process() thread, exactly like message_callback.
 * Must NOT block (it runs inside the MQTT process loop). The recommended
 * action is to set a flag and let the app loop tear down (disconnect → wipe
 * persisted credentials/schema/DP state → restart on-boarding). Do NOT call
 * iot_client_deinit() or iot_client_disconnect() from within this
 * callback — both free the mqtt client that the coreMQTT receive loop is
 * still using on this very stack (it dereferences the context again to send
 * acks and compact the network buffer after the callback returns).
 *
 * Registering this callback opts the client in to consuming protocol-11
 * notices: they then never reach the DP layer or the raw message_callback.
 * Without it they stay on the message_callback path.
 *
 * @param type      Reset classification (remote unbind vs. factory reset).
 * @param user_data The reset_user_data pointer registered with the config.
 */
typedef void (*iot_reset_callback_t)(iot_reset_type_t type, void *user_data);

/**
 * @brief Callback fired when the cloud confirms an OTA upgrade from the app.
 *
 * Fired on the iot_client_process() thread, exactly like message_callback.
 * This means the app user has confirmed the upgrade and the cloud has notified
 * the device over MQTT protocol 15. Keep this callback non-blocking: set a
 * flag, signal a semaphore, or enqueue work for an application worker. Do not
 * call iot_ota_check_upgrade(), download firmware, or write flash here — those
 * operations must run outside the MQTT process loop.
 *
 * Registering this callback opts the client in to consuming protocol-15
 * notices: they then never reach the DP layer or the raw message_callback.
 * Without it they stay on the message_callback path for compatibility.
 *
 * @param channel   Firmware channel from data.firmwareType (0 = main MCU).
 * @param user_data The ota_confirm_user_data pointer registered with the config.
 */
typedef void (*iot_ota_confirm_callback_t)(int channel, void *user_data);

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
    bool mqtt_disable_auto_connect; // false (default) = connect MQTT after init/activation; true = caller invokes iot_client_connect() manually
    const char *cacert;            // CA cert for all TLS (MQTT/HTTPS/IoT-DNS) (PEM, caller-owned, must outlive client)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
    iot_message_callback_t message_callback; // MQTT message callback
    iot_reset_callback_t reset_callback;     // Cloud device-remove (protocol 11) callback
    void *reset_user_data;                   // Opaque pointer passed back to reset_callback
    iot_ota_confirm_callback_t ota_confirm_callback; // APP-confirmed OTA (protocol 15) callback
    void *ota_confirm_user_data;                      // Opaque pointer passed back to ota_confirm_callback

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
    bool mqtt_disable_auto_connect; // false (default) = connect MQTT after init/activation; true = caller invokes iot_client_connect() manually
    const char *cacert;            // CA cert for all TLS (MQTT/HTTPS/IoT-DNS) (PEM, caller-owned, must outlive client)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
    iot_message_callback_t message_callback; // MQTT message callback
    iot_reset_callback_t reset_callback;     // Cloud device-remove (protocol 11) callback
    void *reset_user_data;                   // Opaque pointer passed back to reset_callback
    iot_ota_confirm_callback_t ota_confirm_callback; // APP-confirmed OTA (protocol 15) callback
    void *ota_confirm_user_data;                      // Opaque pointer passed back to ota_confirm_callback
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

    iot_region_t region;           // Server region (AY/AZ/UEAZ/EU/WEAZ/IN/SG)
    iot_env_t env;                 // Environment (PROD or PRE)
    bool mqtt_disable_tls;         // false = mqtts (TLS), true = mqtt (TCP)
    const pal_t *pal;             // PAL adapter

    const char *cacert;           // CA certificate for all TLS (MQTT/HTTPS/IoT-DNS) (caller-owned, points to user buffer/flash)
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (borrowed, NULL = none)
    struct mqtt_client *mqtt;     // Internal MQTT client handle
    iot_message_callback_t message_callback;  // User callback for incoming messages
    iot_reset_callback_t reset_callback;      // Cloud device-remove (protocol 11) callback
    void *reset_user_data;                    // Opaque pointer passed back to reset_callback
    iot_ota_confirm_callback_t ota_confirm_callback; // APP-confirmed OTA (protocol 15) callback
    void *ota_confirm_user_data;                      // Opaque pointer passed back to ota_confirm_callback

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
 * @brief Tell the cloud this device is resetting (unbind), then destroy the client.
 *
 * Calls the cloud's device-reset interface with this device's credentials. On
 * success every resource the client holds is released -- exactly what
 * iot_client_deinit() frees -- and @p client is invalid on return: do not use
 * or free it again.
 *
 * @p scope decides how much the cloud clears, and the two options differ in
 * whether they can be undone:
 * - IOT_RESET_UNBIND_ONLY drops the user-device binding and leaves the device's
 *   cloud-side data in place, so re-pairing can pick it up again.
 * - IOT_RESET_FACTORY additionally discards everything the cloud holds for this
 *   device, business-specific exclusions aside. THIS IS NOT REVERSIBLE:
 *   re-pairing yields a new binding, not the old state.
 *
 * Either way this is not a way to "reconnect cleanly" or to recover from an
 * error -- both give the binding up. For those, disconnect and connect again.
 *
 * On failure NOTHING is destroyed: the client stays fully usable so the caller
 * can retry, or give up and call iot_client_deinit() itself. The return code is
 * therefore "does the cloud know", never "is the client still alive".
 *
 * Two things this does NOT do:
 * - It does not erase persisted state. Credentials, DP state and schema live
 *   wherever the app put them, so wiping them stays the app's job (see
 *   examples/posix/pair/unbind-demo/).
 * - It does not wait for the cloud's protocol-11 notice. That push is what a
 *   *remote* removal looks like; a device-initiated reset is acknowledged by
 *   this call's return code.
 *
 * Must NOT be called from a callback fired by iot_client_process() -- it frees
 * the mqtt client that the coreMQTT receive loop is still using on that stack.
 * See iot_reset_callback_t. Set a flag and reset from the app loop instead.
 *
 * On OPRT_ATOP_BUSINESS_ERROR the cloud's own errorCode decides what to do
 * next, and the two cases are opposite: a terminal code such as
 * GATEWAY_NOT_EXISTS means the binding is already gone, so retrying is
 * pointless and the app should wipe its credentials and re-enter pairing, while
 * REMOTE_API_RUN_UNKNOW_FAILED means the server was busy and the call should be
 * retried. Pass @p error_code to tell them apart; without it the return code
 * alone cannot.
 *
 * @param client         Pointer to iot_client_t instance (must be activated)
 * @param scope          How much to clear (see iot_reset_scope_t); pass
 *                       IOT_RESET_UNBIND_ONLY unless the device is being
 *                       decommissioned
 * @param error_code     Optional buffer receiving the cloud's errorCode; "" when
 *                       the cloud did not send one. NULL if not needed.
 *                       IOT_ATOP_ERROR_CODE_LEN bytes is always enough.
 * @param error_code_len Size of @p error_code in bytes (ignored when NULL)
 * @return OPRT_OK on success (client destroyed);
 *         OPRT_INVALID_PARAMETER if client is NULL;
 *         OPRT_UNINITIALIZED if the client has no device credentials yet;
 *         OPRT_ATOP_BUSINESS_ERROR if the cloud rejected the reset;
 *         other codes on transport failure (client left intact in every
 *         non-OPRT_OK case)
 */
IOT_API int iot_client_reset(iot_client_t *client,
                             iot_reset_scope_t scope,
                             char *error_code, size_t error_code_len);

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
 * @brief Connect to the MQTT broker and subscribe to the device's inbound topic.
 *
 * Needed on two paths: after an init/activation that set `mqtt_disable_auto_connect`
 * false, and to re-establish a link that dropped (pair it with
 * iot_client_disconnect() in the app's reconnect loop).
 *
 * There is no automatic retry and no automatic CA refresh: a TLS failure comes
 * straight back as OPRT_TLS_HANDSHAKE_FAILED. Cert recovery belongs to the app
 * -- on that error code, re-fetch the CA with iot_get_ca_certificate() and
 * reassign client->cacert before reconnecting, or a reconnect loop will retry
 * the same doomed handshake forever after a broker cert rotation. See
 * examples/posix/dp-management/ for the shape of that loop.
 *
 * @param client Pointer to iot_client_t instance (must have mqtt_url and devid set)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client/url/devid is missing
 */
IOT_API int iot_client_connect(iot_client_t *client);

/**
 * @brief Disconnect from the MQTT broker and destroy the MQTT client.
 *
 * Safe to call with NULL or when not connected (no-op). Must NOT be called
 * from inside a callback fired by iot_client_process() — see
 * iot_reset_callback_t for why.
 *
 * @param client Pointer to iot_client_t instance (NULL is safe)
 */
IOT_API void iot_client_disconnect(iot_client_t *client);

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
 * @param client             Pointer to iot_client_t instance (must not be NULL)
 * @param host               Target host to get certificate for
 * @param port               Target port
 * @param ca_certificate     Caller-provided buffer receiving the NUL-terminated
 *                           CA certificate PEM string (a single CA PEM is
 *                           typically 1-2 KB; 4096 bytes is a safe size)
 * @param ca_certificate_len Size of the ca_certificate buffer in bytes
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client/host/ca_certificate
 *         is NULL or ca_certificate_len is 0, OPRT_INVALID_RESULT if no CA cert
 *         is available or the buffer is too small
 */
IOT_API int iot_get_ca_certificate(iot_client_t *client, const char *host, uint16_t port, char *ca_certificate, size_t ca_certificate_len);

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
 * @brief Get the QR code activation URL from Tuya cloud.
 *
 * Resolves the ATOP endpoint and CA certificate via IoT DNS automatically —
 * does not require an initialized client.
 *
 * @param[in]  request  Request parameters (uuid, authkey, app_id, type, region, env)
 * @param[out] url      Caller-provided buffer receiving the NUL-terminated URL
 * @param[in]  url_len  Size of the url buffer in bytes
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if request or url is NULL
 *         or url_len is 0, OPRT_INVALID_RESULT if the buffer is too small
 */
IOT_API int iot_get_qrcode_info(const iot_qrcode_request_t *request, char *url, size_t url_len);

#endif /* _IOT_CLIENT_H_ */
