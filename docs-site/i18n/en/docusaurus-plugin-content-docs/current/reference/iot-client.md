---
title: IoT Client API Reference
sidebar_label: IoT Client
sidebar_position: 3
---

# IoT Client API Reference

The IoT Client module (CMake target `tuya_iot_client`, artifact `libtuya_iot_client.a`) provides device Activation, MQTT connection, and session-token retrieval. Header: `modules/iot-client/include/iot_client.h`.

:::caution Required initialization
Before using any SDK function, you must call [`iot_init()` or `iot_init_default()`](#iot_init--iot_init_default) to initialize the PAL. If it is not initialized, `iot_client_init()` returns `NULL` immediately, and `iot_get_qrcode_info()` returns `OPRT_UNINITIALIZED`.
:::

## Additional Notes on Activation Requests {#激活请求补充说明}

During device Activation, the underlying implementation calls `atop_activate_request()` to send a request to `thing.device.opensdk.active`. This ATOP interface uses `activite_request_t` to organize request parameters, and dynamically constructs the `options` field from the input parameters.

- When `sdk_version` is not empty, `options` includes `sdkFullVer`
- `otaChannel` is always `0`
- `isFK` switches between `true` and `false` depending on whether `firmware_key` is present
- When `firmware_key` is provided, `productKeyStr` is also reported

Example:

```json
{"otaChannel":0,"sdkFullVer":"agentic-kit_0.1.0","isFK":false}
```

The default source of `sdkFullVer` is the `SDK_VERSION` macro. The current on-boarding workflow writes it to `activite_request_t.sdk_version`; callers that construct `activite_request_t` themselves can explicitly provide another version value. See `modules/iot-client/src/atop.h` and `modules/iot-client/src/atop.c` for the relevant definitions.

## Error Codes {#错误码}

| Value | Macro | Description |
|----|-----|------|
| 0 | `OPRT_OK` | Success |
| -1 | `OPRT_COMMUNICATION_ERROR` | Communication error |
| -2 | `OPRT_INVALID_PARAMETER` | Invalid parameter |
| -3 | `OPRT_INVALID_RESULT` | Invalid result |
| -4 | `OPRT_UNINITIALIZED` | Not initialized |
| -5 | `OPRT_NOT_SUPPORTED` | Not supported |
| -6 | `OPRT_MALLOC_FAILED` | Memory allocation failed |
| -7 | `OPRT_TLS_HANDSHAKE_FAILED` | TLS handshake failed |

### MQTT Status Codes (`MQTTStatus_t`) {#mqtt-状态码mqttstatus_t}

These are the numbers in parentheses in logs such as `MQTT_Connect failed: MQTTServerRefused (6)`. **Current logs also print the name**, so this table is mainly for consulting older logs and historical tickets that contain only a bare number.

| Value | Name | Meaning in this SDK |
|----|------|------|
| 0 | `MQTTSuccess` | Success |
| 1 | `MQTTBadParameter` | Invalid parameter (a local issue; the request does not reach the network) |
| 2 | `MQTTNoMemory` | Insufficient buffer space for the packet being sent or received |
| 3 | `MQTTSendFailed` | Underlying send failed--the network is disconnected or the TLS session is invalid |
| 4 | `MQTTRecvFailed` | Underlying receive failed, for the same reasons as above |
| 5 | `MQTTBadResponse` | A packet was received but is malformed (the peer is not a valid MQTT broker, or packets were corrupted/mixed on the connection) |
| 6 | `MQTTServerRefused` | The **broker explicitly rejected** CONNECT or SUBSCRIBE--the network is fully operational, but the business layer does not allow the operation. See the table below |
| 7 | `MQTTNoDataAvailable` | No data is available in this call; this is a normal polling result |
| 8 | `MQTTIllegalState` | Invalid state-machine state |
| 9 | `MQTTStateCollision` | QoS packet ID collision |
| 10 | `MQTTKeepAliveTimeout` | Timed out waiting for PINGRESP; the connection is dead even though the socket reported no error |
| 11 | `MQTTNeedMoreBytes` | The packet is incomplete and the function must be called again (not an error) |

### CONNACK Rejection Reasons {#connack-拒绝原因}

`MQTTServerRefused (6)` only indicates that the connection "was rejected." The broker provides the specific reason in CONNACK. A rejected connection actually logs four lines, and the **first line** gives the reason:

```
10:15:13 [E] [mqtt] Connection refused: bad user name or password.
10:15:13 [E] [mqtt] CONNACK recv failed with status = MQTTServerRefused.
10:15:13 [E] [mqtt] MQTT connection failed with status = MQTTServerRefused.
10:15:13 [E] [iot] MQTT_Connect failed: MQTTServerRefused (6)
```

The first three lines come from coreMQTT (`[mqtt]`), and the last comes from the SDK (`[iot]`). The timestamp prefix is added by the default output; if the application takes over the dispatch by defining `AGENTIC_KIT_LOG`, the output shape is whatever that macro expands to.

| CONNACK code | Message | Common cause and action |
|----|------|------|
| 1 | unacceptable protocol version | The broker does not support MQTT 3.1.1; this should almost never occur |
| 2 | identifier rejected | The clientId was rejected--check whether the devid is complete (normally 20-22 bytes) |
| 3 | server unavailable | The cloud is temporarily unavailable; retry with backoff. This is unrelated to credentials |
| 4 | bad user name or password | The credentials are not recognized |
| 5 | not authorized | Not authorized |

Codes 4 and 5 are the most common, and **in most cases the password calculation is not wrong**. Instead, the device has been unbound or deleted in the cloud: the credential format and algorithm are correct, but the server no longer recognizes this `devid`. To confirm quickly, use the same credentials to call an ATOP interface over HTTP (for example, use the `iot_atop_call` generic call for `tuya.device.schema.newest.get`). ATOP returns the cloud `errorCode` unchanged, which carries much more information than CONNACK. If that request is also rejected, the device must be provisioned and activated again.

:::note
Lines prefixed with `[mqtt]` come from coreMQTT and are connected to the logging facade through `common/core_mqtt_config.h`. If these lines do not appear, the build still includes `MQTT_DO_NOT_USE_CUSTOM_CONFIG`; the rejection reason is discarded, leaving only the bare `6`.
:::

## Enumerations {#枚举类型}

### Region (`iot_region_t`) {#regioniot_region_t}

| Value | Name | Description |
|----|------|------|
| 0 | `AY` | China (Shanghai) |
| 1 | `AZ` | Western United States (Oregon) |
| 2 | `UEAZ` | Eastern United States (Virginia) |
| 3 | `EU` | Europe (Frankfurt) |
| 4 | `WEAZ` | Western Europe (Eemshaven, Netherlands) |
| 5 | `IN` | India (Mumbai) |
| 6 | `SG` | Southeast Asia (Singapore) |

Note: If a device is provisioned using the Tuya Smart or Smart Life app, the Eastern United States and Western Europe data centers are not currently supported. API-based provisioning supports them.

### Environment (`iot_env_t`) {#environmentiot_env_t}

| Value | Name | Description |
|----|------|------|
| 0 | `PROD` | Production environment |
| 1 | `PRE` | Pre-release environment |
| 2 | `TEST` | Test environment |

### Reset Type (`iot_reset_type_t`) {#reset-typeiot_reset_type_t}

Classification for cloud device-removal notifications (protocol 11), received by `reset_callback`.

| Value | Name | Description |
|----|------|------|
| 0 | `IOT_RESET_REMOTE_UNBIND` | The user removed the device in the App (it can be provisioned and bound again) |
| 1 | `IOT_RESET_REMOTE_FACTORY` | The cloud issued a factory-reset request |

### Log Level (`log_level_t`) {#log-levellog_level_t}

Logging is controlled through the global logging facade in `common/log.h`: log volume is decided solely by the compile-time `AGENTIC_KIT_LOG_LEVEL` (there is no runtime level); where lines land is a compile-time decision too — define `AGENTIC_KIT_LOG` to dispatch every line into your own macro, which may print or drop by level.

Note: `log_level_t` is not an enum; it is a `typedef int` (so `LOG_*` can be used in preprocessor `#if` conditions). The names in the following table are macros.

| Value | Name |
|----|------|
| 0 | `LOG_NONE` |
| 1 | `LOG_ERROR` |
| 2 | `LOG_WARN` |
| 3 | `LOG_INFO` |
| 4 | `LOG_DEBUG` |

## Configuration Structures {#配置结构体}

### `iot_client_config_t` {#iot_client_config_t}

Initialization configuration for an activated device.

| Field | Type | Description |
|------|------|------|
| `devid` | `char[32]` | Device ID |
| `secret_key` | `char[32]` | Device secret key |
| `local_key` | `char[32]` | Local encryption key |
| `region` | `iot_region_t` | Data-center region |
| `env` | `iot_env_t` | Environment |
| `mqtt_disable_tls` | `bool` | `false` (default) uses MQTTS; `true` uses plaintext MQTT |
| `mqtt_disable_auto_connect` | `bool` | `false` (default) connects to MQTT automatically after initialization; when `true`, you must call [`iot_client_connect()`](#iot_client_connect) manually |
| `skip_version_report` | `bool` | `false` (default) reports the SDK metadata and firmware version during initialization; `true` skips these two reports (set only when the cloud already has the current version) |
| `cacert` | `const char *` | CA certificate PEM (for MQTT/HTTPS/IoT-DNS TLS); owned by the caller and must remain valid for the client's lifetime |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | Platform certificate-bundle callback (such as ESP-IDF's `esp_crt_bundle_attach`); NULL means no bundle is used. See [TLS Certificate Verification](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT message callback; can be NULL |
| `reset_callback` | `iot_reset_callback_t` | Callback for cloud unbind/factory-reset notifications (protocol 11); can be NULL. Once registered, protocol 11 messages are consumed by the SDK and are no longer delivered to `message_callback` |
| `reset_user_data` | `void *` | User pointer passed through to `reset_callback`; can be NULL |
| `ota_confirm_callback` | `iot_ota_confirm_callback_t` | Callback for App-confirmed OTA upgrade notifications (protocol 15); can be NULL. Once registered, protocol 15 messages are consumed by the SDK and are no longer delivered to `message_callback` |
| `ota_confirm_user_data` | `void *` | User pointer passed through to `ota_confirm_callback`; can be NULL |
| `schema` | `const char *` | Data Point (DP) Schema JSON used to restore the Schema after restart; owned by the caller (`NULL` = do not restore / loose mode) |
| `schema_id` | `const char *` | Persisted Schema ID (the stable key for Schema upgrade queries; can be NULL) |
| `dp_state` | `const char *` | Persisted current DP state, `{"dps":{...}}`, used for restoration (does not mark DPs dirty or Report them; can be NULL) |
| `sw_ver` | `const char *` | Application firmware version (such as `"1.2.3"`), automatically reported during `iot_client_init` for cloud OTA comparison; NULL uses the SDK default `IOT_SDK_SW_VER`. See [OTA Upgrade](../guides/ota-upgrade.md) |

### `iot_on_boarding_config_t` {#iot_on_boarding_config_t}

Configuration for device provisioning and Activation.

| Field | Type | Description |
|------|------|------|
| `uuid` | `char[32]` | Device UUID (authorization code obtained from the Tuya platform) |
| `authkey` | `char[64]` | Auth Key |
| `product_key` | `char[32]` | Product PID |
| `firmware_key` | `char[64]` | Firmware Key (can be empty) |
| `modules` | `const char *` | Module information (can be NULL) |
| `feature` | `const char *` | Feature information (can be NULL) |
| `skill_param` | `const char *` | Skill parameters (can be NULL) |
| `timeout_ms` | `int` | Activation timeout in milliseconds |
| `env` | `iot_env_t` | Environment: `PROD` (default) or `PRE` |
| `mqtt_disable_tls` | `bool` | TLS switch |
| `mqtt_disable_auto_connect` | `bool` | `false` (default) connects to MQTT automatically after Activation; when `true`, you must call [`iot_client_connect()`](#iot_client_connect) manually |
| `skip_version_report` | `bool` | `false` (default) reports the SDK metadata and firmware version after Activation; `true` skips these two reports (set only when the cloud already has the current version) |
| `cacert` | `const char *` | CA certificate PEM (for MQTT/HTTPS/IoT-DNS TLS); owned by the caller |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | Platform certificate-bundle callback (such as ESP-IDF's `esp_crt_bundle_attach`); NULL means no bundle is used. See [TLS Certificate Verification](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT message callback |
| `reset_callback` | `iot_reset_callback_t` | Callback for cloud unbind/factory-reset notifications (protocol 11); can be NULL. Once registered, protocol 11 messages are consumed by the SDK and are no longer delivered to `message_callback` |
| `reset_user_data` | `void *` | User pointer passed through to `reset_callback`; can be NULL |
| `ota_confirm_callback` | `iot_ota_confirm_callback_t` | Callback for App-confirmed OTA upgrade notifications (protocol 15); can be NULL. Once registered, protocol 15 messages are consumed by the SDK and are no longer delivered to `message_callback` |
| `ota_confirm_user_data` | `void *` | User pointer passed through to `ota_confirm_callback`; can be NULL |
| `sw_ver` | `const char *` | Application firmware version (such as `"1.2.3"`), automatically reported after Activation for cloud OTA comparison; NULL uses the SDK default `IOT_SDK_SW_VER`. See [OTA Upgrade](../guides/ota-upgrade.md) |

### `iot_client_t` (Returned Instance) {#iot_client_t返回实例}

The client instance returned by `iot_client_init()` or a provisioning API contains these key fields:

| Field | Type | Description |
|------|------|------|
| `devid` | `char[32]` | Device ID assigned after Activation |
| `secret_key` | `char[32]` | MQTT authentication key |
| `local_key` | `char[32]` | Local encryption key |
| `region` | `iot_region_t` | Server region |
| `env` | `iot_env_t` | Environment |

## API Functions {#api-函数}

### `iot_init` / `iot_init_default` {#iot_init--iot_init_default}

```c
int iot_init(const pal_t *pal);
int iot_init_default(void);
```

Initializes the IoT SDK's platform abstraction layer (PAL) and **must be called before any other SDK function**. `iot_init_default()` uses the built-in default PAL adapter (POSIX / FreeRTOS); `iot_init()` uses a custom PAL (see [Porting to a New Platform](../guides/porting-to-new-platform.md)).

**Return values:** `OPRT_OK` on success; `iot_init()` returns `OPRT_INVALID_PARAMETER` if `pal` is NULL or a required function pointer is missing.

---

### `iot_client_init` {#iot_client_init}

```c
iot_client_t *iot_client_init(const iot_client_config_t *config);
```

Initializes the IoT Client with existing device credentials (devid, secret_key, local_key) and resolves the MQTT/HTTPS endpoints. By default, it establishes the MQTT connection automatically; when `mqtt_disable_auto_connect = true`, you must call [`iot_client_connect()`](#iot_client_connect) manually. **Note:** If automatic connection fails, `iot_client_init()` releases the client and returns `NULL`. Devices whose network might not be ready at startup should therefore explicitly disable automatic connection and manage reconnection themselves.

**Return value:** An `iot_client_t *` on success; `NULL` on failure.

---

### `iot_client_init_on_boarding` {#iot_client_init_on_boarding}

```c
iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);
```

Blocks while waiting for App QR-code Activation. Internally, it listens for the Activation event over MQTT and, after successful Activation, returns a client instance containing `devid`, `secret_key`, and `local_key`.

**Return value:** An `iot_client_t *` on success; `NULL` on timeout or failure.

---

### `iot_client_init_on_boarding_with_token` {#iot_client_init_on_boarding_with_token}

```c
iot_client_t *iot_client_init_on_boarding_with_token(
    const iot_on_boarding_config_t *config,
    const char *token);
```

Starts Activation directly with a known Activation Token, skipping the MQTT wait. The Region is derived automatically from the token's first two characters.

**Parameters:**
- `config` - Provisioning configuration
- `token` - Activation Token (format: `{region}{token}{secret}`, for example `AYH73H8u7Ap4pX`)

**Return value:** An `iot_client_t *` on success; `NULL` on failure.

---

### `iot_client_reset` {#iot_client_reset}

```c
int iot_client_reset(iot_client_t *client, iot_reset_scope_t scope,
                     char *error_code, size_t error_code_len);
```

Tells the cloud that this device is being reset. It calls the `tuya.device.reset` ATOP interface (version `5.0`) with this fixed request body:

```json
{"resetFactory":true,"t":1756108800}
```

The `scope` parameter determines `resetFactory`, and the two choices have **completely different reversibility**:

| `scope` | `resetFactory` | Cloud behavior | Reversibility |
|---|---|---|---|
| `IOT_RESET_UNBIND_ONLY` | `false` | Removes only the user-device binding; the device's cloud data is retained | Provisioning again can reconnect the original data |
| `IOT_RESET_FACTORY` | `true` | In addition to unbinding, **deletes all data associated with the device** (except where specific business rules state otherwise) | **Irreversible** |

These have the same pair of meanings as `IOT_RESET_REMOTE_UNBIND` / `IOT_RESET_REMOTE_FACTORY` in cloud downlink protocol 11, but in the opposite direction (that notification is the classification pushed from the cloud to the device; this parameter is the choice sent from the device to the cloud).

:::danger IOT_RESET_FACTORY is irreversible
`IOT_RESET_FACTORY` causes the cloud to delete **all data associated with this device**, with no means of recovery. Provisioning again creates a new binding; it does not restore the previous state. Use it only when **retiring a device or transferring it to a new user**.

For routine cases (the device cleans itself up after the user unbinds it in the App, or the device is rebound), use `IOT_RESET_UNBIND_ONLY`.
:::

:::caution Neither scope means "reconnect"
Both scopes relinquish the binding. To reconnect cleanly, use [`iot_client_disconnect()`](#iot_client_disconnect) followed by [`iot_client_connect()`](#iot_client_connect).
:::

**Success or failure determines ownership of the client**--this is the most important aspect of this interface:

| Return | Client state | Caller action |
|---|---|---|
| `OPRT_OK` | **Destroyed** (all resources released, equivalent to `iot_client_deinit()`) | Do not use the pointer again; do not disconnect or deinitialize it |
| Any other value | **Intact and usable** | Retry, or call `iot_client_deinit()` to clean it up |

In other words, the return code answers "does the cloud know?", not "is the client still alive?" Deliberately preserving the client on failure prevents a worse state in which the device is locally unbound while the cloud still considers it bound.

This interface does **not** do either of the following:

1. **It does not erase persisted data.** Only the application knows where credentials, DP state, and the Schema are stored, so erasing them remains the application's responsibility (see `examples/posix/pair/unbind-demo/`).
2. **It does not wait for a protocol 11 notification.** That push represents "the cloud/user removed the device from the App." A device-initiated reset is confirmed by this call's return code and does not produce a separate notification.

:::warning
Do not call this function from a callback triggered by `iot_client_process()` (`message_callback`, `reset_callback`, or `ota_confirm_callback`). It frees the MQTT client that the coreMQTT receive loop is still using on its current stack. Set a flag instead, return to the application's main loop, and reset the client there.
:::

### Always Inspect `error_code` When Rejected {#被拒绝时必须看-error_code}

`OPRT_ATOP_BUSINESS_ERROR` only means "the cloud rejected the request," but the two rejection types require **opposite actions**. The return code alone cannot distinguish them; this is the sole reason for the `error_code` output parameter:

| errorCode | Meaning | Correct action |
|---|---|---|
| `REMOTE_API_RUN_UNKNOW_FAILED` | Server busy | Retry with backoff |
| `GATEWAY_NOT_EXISTS` | The device no longer exists in the cloud (the binding is already absent) | **Retry will never succeed**: erase local credentials and provision again |

Treating the latter as retryable causes the device to retry forever without ever returning to provisioning: credentials are never erased and provisioning never starts, effectively bricking the device.

```c
char err[64] = {0};
/* Use IOT_RESET_FACTORY to retire a device; use IOT_RESET_UNBIND_ONLY for routine unbinding */
int rc = iot_client_reset(client, IOT_RESET_UNBIND_ONLY, err, sizeof(err));
if (rc == OPRT_OK) {
    /* The client has been destroyed; only erase the credentials you persisted */
} else if (strcmp(err, "GATEWAY_NOT_EXISTS") == 0) {
    wipe_credentials();          /* Do not retry */
    enter_pairing_mode();
} else {
    /* The client remains intact and can be retried */
}
```

**Parameters:**
- `client` - Activated IoT Client instance
- `scope` - Cleanup scope; see the table above. Always use `IOT_RESET_UNBIND_ONLY` except when retiring a device
- `error_code` - Optional buffer that receives the cloud errorCode; `""` if the cloud provides none. Pass `NULL` when it is not needed. `IOT_ATOP_ERROR_CODE_LEN` (48) bytes is sufficient
- `error_code_len` - Size of the `error_code` buffer (ignored when `error_code` is NULL)

**Return values:** `OPRT_OK` on success (the client has been destroyed); `OPRT_INVALID_PARAMETER` if client is NULL; `OPRT_UNINITIALIZED` if there are no device credentials yet (not activated); `OPRT_ATOP_BUSINESS_ERROR` if the cloud rejected the request (see the table above); other values indicate transport-layer errors.

---

### `iot_client_deinit` {#iot_client_deinit}

```c
void iot_client_deinit(iot_client_t *client);
```

Deinitializes the IoT Client, disconnects MQTT, and releases all resources.

---

### `iot_client_connect` {#iot_client_connect}

```c
int iot_client_connect(iot_client_t *client);
```

Connects to the MQTT broker and subscribes to the device's inbound topic. Call it in either of these cases:

1. `mqtt_disable_auto_connect = true` was set during initialization/Activation, so the application must establish the connection;
2. Reconnecting after the connection is lost--pair it with [`iot_client_disconnect()`](#iot_client_disconnect) in the application's own reconnection loop.

**It does not retry automatically or refresh the CA certificate automatically**: a TLS handshake failure returns `OPRT_TLS_HANDSHAKE_FAILED` immediately. Certificate recovery is the application's responsibility. After receiving this error, first use `iot_get_ca_certificate()` to retrieve the certificate again and reassign `client->cacert`, then reconnect. Otherwise, after a broker certificate rotation, the reconnection loop retries the same handshake that is guaranteed to fail forever. See `examples/posix/dp-management/` for an example.

**Parameter:** `client` - IoT Client instance (with `mqtt_url` and `devid` already set)

**Return values:** `OPRT_OK` on success; `OPRT_INVALID_PARAMETER` if client / URL / devid is missing; otherwise an error code.

---

### `iot_client_disconnect` {#iot_client_disconnect}

```c
void iot_client_disconnect(iot_client_t *client);
```

Disconnects MQTT and destroys the MQTT client. Passing `NULL` or calling it while disconnected is a safe no-op; repeated calls are safe.

:::warning
Do not call this function or `iot_client_deinit()` from a callback triggered by `iot_client_process()` (`message_callback`, `reset_callback`, or `ota_confirm_callback`). Both functions free the MQTT client that the coreMQTT receive loop is still using on its current stack: after the callback returns, the loop dereferences that context again to return the acknowledgment and organize the network buffer. Set a flag instead and let the application's main loop disconnect.
:::

**Parameter:** `client` - IoT Client instance (`NULL` is safe)

---

### `iot_client_get_session_token` {#iot_client_get_session_token}

```c
int iot_client_get_session_token(iot_client_t *client, const char *agent_code, char *token, size_t token_len);
```

Obtains an AI session token (session_token) from the Tuya cloud for creating an STM Open SDK Session.

**Parameters:**
- `client` - IoT Client instance
- `agent_code` - Agent code (pass `NULL` to use the default Agent)
- `token` - Output buffer that receives the session-token string
- `token_len` - Output-buffer size in bytes

**Return values:** `OPRT_OK` on success; otherwise an error code.

---

### `iot_client_process` {#iot_client_process}

```c
int iot_client_process(iot_client_t *client, uint32_t timeout_ms);
```

Processes MQTT events (receiving messages and maintaining keepalive). Call this function in a loop when MQTT messages need to be received.

**Parameters:**
- `client` - IoT Client instance
- `timeout_ms` - Processing timeout in milliseconds

**Return values:** `OPRT_OK` on success; `OPRT_INVALID_PARAMETER` if `client` is NULL; `OPRT_UNINITIALIZED` when there is no MQTT connection.

---

### `iot_client_publish` {#iot_client_publish}

```c
int iot_client_publish(iot_client_t *client, const uint8_t *data, size_t data_len);
```

Publishes an encrypted message to `smart/device/out/{deviceid}`.

**Parameters:**
- `client` - IoT Client instance
- `data` - Plaintext data (encrypted internally)
- `data_len` - Data length

**Return values:** `OPRT_OK` on success; `OPRT_INVALID_PARAMETER` if `client` is NULL, `data` is NULL, or `data_len` is 0; `OPRT_UNINITIALIZED` when there is no MQTT connection; `OPRT_MALLOC_FAILED` if allocation of the encryption buffer fails; `OPRT_COMMUNICATION_ERROR` if encryption or publishing fails.

---

### `iot_get_qrcode_info` {#iot_get_qrcode_info}

```c
int iot_get_qrcode_info(const iot_qrcode_request_t *request, char *url, size_t url_len);
```

Obtains a provisioning and Activation URL from the Tuya cloud. The device can encode this URL as a QR code and display it to the user.

**Request parameters in `iot_qrcode_request_t`:**

| Field | Type | Description |
|------|------|------|
| `uuid` | `const char *` | Device UUID |
| `authkey` | `const char *` | Auth Key |
| `app_id` | `const char *` | App ID (can be an empty string) |
| `type` | `int` | QR-code type (usually 1) |
| `region` | `iot_region_t` | Data-center region |
| `env` | `iot_env_t` | Environment |
| `cacert` | `const char *` | CA certificate PEM (for HTTPS/IoT-DNS TLS); owned by the caller |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | Platform certificate-bundle callback (such as ESP-IDF's `esp_crt_bundle_attach`); NULL means no bundle is used. See [TLS Certificate Verification](../guides/tls-cert-verification.md) |

**Output parameters:**
- `url` - Caller-allocated buffer that receives the NUL-terminated Activation URL
- `url_len` - Buffer size in bytes

**Return values:** `OPRT_OK` on success; `OPRT_INVALID_PARAMETER` if `request`/`url` is NULL or `url_len` is 0; `OPRT_INVALID_RESULT` if the buffer is not large enough.

---

### `iot_get_ca_certificate` {#iot_get_ca_certificate}

```c
int iot_get_ca_certificate(iot_client_t *client, const char *host, uint16_t port,
                           char *ca_certificate, size_t ca_certificate_len);
```

Obtains the CA certificate for the target host.

**Parameters:**
- `client` - IoT Client instance (cannot be NULL)
- `host` - Target host name
- `port` - Target port
- `ca_certificate` - Caller-allocated buffer that receives the NUL-terminated CA certificate PEM string (a single CA PEM is normally 1-2KB; 4096 bytes is recommended)
- `ca_certificate_len` - Buffer size in bytes

**Return values:** `OPRT_OK` on success; `OPRT_INVALID_PARAMETER` if `client`/`host`/`ca_certificate` is NULL or `ca_certificate_len` is 0; `OPRT_INVALID_RESULT` if no certificate is available or the buffer is not large enough.

---

### Logging Configuration {#日志配置}

The IoT Client uses the global logging facade provided by `common/log.h` and no longer provides a separate API for configuring a log callback.

```c
// Log volume is a compile-time decision (-DAGENTIC_KIT_LOG_LEVEL=N);
// there is no runtime level

// The destination is a compile-time decision too: in your
// agentic_kit_config.h, remap the dispatch into your own macro
// (print or drop by level inside it, as you like)
#define AGENTIC_KIT_LOG(level, tag, fmt, ...) \
    my_log(level, tag, fmt, ##__VA_ARGS__)
```
