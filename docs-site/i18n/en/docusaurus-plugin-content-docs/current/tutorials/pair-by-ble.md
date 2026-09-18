---
title: BLE Provisioning
sidebar_label: BLE Provisioning
sidebar_position: 7
---

# BLE Provisioning

> Corresponding example: `examples/esp-idf/pair/pair-by-ble/`

This chapter explains how to Provision a device over BLE (Bluetooth Low Energy) on an embedded device. The Tuya App passes the WiFi credentials and Provisioning token to the device over a BLE connection, so the device can complete Provisioning without a camera or a screen.

## Applicable scenarios {#适用场景}

- The device supports Bluetooth (BLE 4.2+, NimBLE stack)
- The device has no camera or screen but needs to be Provisioned through the Tuya App
- The device is not yet connected to WiFi (the core purpose of BLE Provisioning is to deliver WiFi credentials)

:::note Platform note
The current example is based on ESP-IDF + the NimBLE stack; other platforms must adapt the BLE layer themselves.
:::

## Overall flow {#整体流程}

```
nvs_flash_init()                    // 1. Initialize NVS (required by BLE)
        |
        v
tuya_ble_nimble_start(&prov_cfg)    // 2. Start BLE advertising and wait for the App to connect
        |
        v
(The App sends the WiFi SSID/password/Token over BLE)
        |
        v
on_tuya_ble_prov_complete(creds)    // 3. Callback: WiFi credentials and Token received
        |
        v
tuya_ble_nimble_stop()              // 4. Stop BLE advertising
        |
        v
(Connect to the network using the WiFi credentials)  // 5. Connect to WiFi
        |
        v
iot_client_init_on_boarding_with_token(token)  // 6. Activate the device with the Token
```

:::warning The device must connect to MQTT before the App considers Provisioning successful
**The App considers Provisioning successful only after it detects that the device has connected to the Tuya cloud MQTT channel (the device is online).**
Completing only Token Activation and obtaining credentials such as `devid` without connecting to MQTT makes the App show Provisioning failure/timeout.

Therefore this is now guaranteed by the default behavior: auto-connect is enabled by default and needs no extra configuration. As long as `.mqtt_disable_auto_connect` is not set, the device automatically connects to MQTT after Activation completes (the example `main/main.c` is already configured this way); only if the application explicitly sets `.mqtt_disable_auto_connect = true` must it call `iot_client_connect()` manually immediately after Activation succeeds.
:::

## Key code {#关键代码}

### Configuration and startup {#配置与启动}

```c
#include "tuya_ble_nimble.h"
#include "app_config.h"

static void on_tuya_ble_prov_complete(const tuya_ble_wifi_creds_t *creds)
{
    // WiFi credentials received from the App
    // creds is valid only during the callback; copy it into an application-owned buffer
    // (the example logs at DEBUG, so the SDK's [ble] protocol log prints the raw
    // credentials JSON including the password/Token, which helps on-device debugging)
    s_wifi_creds = *creds;
    // Notify the main thread to continue
    xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);
}

void app_main(void)
{
    // Initialize NVS (required by NimBLE)
    nvs_flash_init();
    iot_init_default(); // Initialize the PAL/cJSON allocator before BLE JSON parsing

    tuya_ble_prov_cfg_t prov_cfg = {
        .device_name = "TYBLE",         // BLE advertising name (max 5 characters; longer names are truncated)
        .product_key = PRODUCT_KEY,     // Product PID
        .uuid        = DEVICE_UUID,     // Device UUID
        .auth_key    = AUTH_KEY,        // Device Auth Key
        .cb          = on_tuya_ble_prov_complete,
    };

    tuya_ble_nimble_start(&prov_cfg);

    // Wait for Provisioning to complete
    xEventGroupWaitBits(s_prov_event_group, PROV_DONE_BIT,
                        pdTRUE, pdFALSE, portMAX_DELAY);

    tuya_ble_nimble_stop();

    // Next: connect to WiFi using creds->ssid/password
    // Then call iot_client_init_on_boarding_with_token() with creds->token
}
```

### Configuration file `app_config.h` {#配置文件-app_configh}

```c
#define TUYA_BLE_DEVICE_NAME  "TYBLE"   // max 5 characters (TUYA_BLE_NAME_MAX_LEN); longer values are truncated
#define PRODUCT_KEY           "your_product_key"
#define DEVICE_UUID           "your_uuid"
#define AUTH_KEY              "your_auth_key"
```

> Note: the current example reads these configuration items directly from `main/app_config.h`; refer to the actual example file in the repository.

## API reference {#api-参考}

### `tuya_ble_nimble_start` {#tuya_ble_nimble_start}

```c
int tuya_ble_nimble_start(const tuya_ble_prov_cfg_t *cfg);
```

Starts BLE advertising and the GATT service, and waits for the Tuya App to connect and deliver Provisioning information.

**Return value:** `0` on success; nonzero indicates an error.

**`tuya_ble_prov_cfg_t` fields:**

| Field | Type | Description |
|------|------|------|
| `device_name` | `const char *` | BLE advertising device name (max 5 characters, `TUYA_BLE_NAME_MAX_LEN`; the excess is silently truncated) |
| `product_key` | `const char *` | Product PID |
| `uuid` | `const char *` | Device UUID |
| `auth_key` | `const char *` | Device Auth Key |
| `cb` | callback | Provisioning-complete callback |

### `tuya_ble_nimble_stop` {#tuya_ble_nimble_stop}

```c
int tuya_ble_nimble_stop(void);
```

Stops BLE advertising and services, waits for any already-accepted WiFi scan task to finish, then releases the queues and NimBLE resources. Start/stop must be called serially by the application task; stop must not be called from a BLE callback or the ESP event loop. Scans are cancelled logically, so if the driver does not return for a long time, stop also waits.

**Return value:** `0` means stopped (calling stop again also returns `0`); nonzero means `nimble_port_stop()` failed and resources are kept so it can be retried; a failure of `nimble_port_deinit()` (which is almost impossible after a clean stop) aborts the program through `ESP_ERROR_CHECK`. After a successful stop, the example first stops the WiFi started during the scan phase, then configures the credentials and restarts STA, without relying on calling start again on an already-started STA to produce a connection event.

### `tuya_ble_wifi_creds_t` {#tuya_ble_wifi_creds_t}

The struct received in the callback:

| Field | Type | Description |
|------|------|------|
| `ssid` | `char[]` | WiFi SSID |
| `password` | `char[]` | WiFi password |
| `token` | `char[]` | Provisioning token (used for subsequent device Activation) |

## Build and run {#编译与运行}

```sh
cd examples/esp-idf/pair/pair-by-ble
idf.py set-target esp32s3   # Choose according to your chip
idf.py build
idf.py flash monitor
```

## sdkconfig highlights {#sdkconfig-要点}

Key settings currently present in `sdkconfig.defaults`:

- `CONFIG_BT_ENABLED=y` — Enable Bluetooth
- `CONFIG_BT_NIMBLE_ENABLED=y` — Use the NimBLE stack
- `CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y` — Enable BLE controller mode only

## Notes {#注意事项}

- After BLE Provisioning completes, stop BLE advertising (`tuya_ble_nimble_stop`) as soon as possible to avoid RF conflicts when coexisting with WiFi.
- The token format is the same as for the other Provisioning methods: the first two characters are the Region code.
- The device Activation flow after Provisioning completes is the same as in [Device QR-code Provisioning](./scan-by-device), using `iot_client_init_on_boarding_with_token()`.
- Never set `.mqtt_disable_auto_connect = true` in the Activation configuration: **the App treats the device coming online on MQTT as the condition for Provisioning success**, so without an MQTT connection the App shows Provisioning failure/timeout.
- Make sure the project correctly references the `modules/tuya-ble/` and `modules/iot-client/` components.

## BLE core porting conventions {#ble-核心移植约定}

Configuration strings must remain valid for the lifetime of the core state and be NUL-terminated: `product_key` is 16 bytes, `auth_key` is 32 bytes, and `uuid` is 16 bytes or 20 alphanumeric characters. After upgrading, recompile callers, because the layout of the public state struct has changed.

Call `tuya_ble_prov_on_data()`, `tuya_ble_prov_tx_ready()`, `tuya_ble_prov_tick()`, and `tuya_ble_prov_close()` on the same BLE execution context. On connection close, reconnect, or stack reset, call `close()` to clear the session keys and queued data; the legacy `reset_conn()` only resets transport state. `set_paired(true)` does not grant permission to deliver credentials.

Notifications are at most 20 bytes by default; after MTU negotiation, update the actual budget with `tuya_ble_prov_set_gatt_payload(state, mtu - 3)`. The send callback must copy the data before returning: returning 0 means accepted, `TUYA_BLE_SEND_BUSY` means not accepted and retry later, and any other value means permanent failure. The port periodically passes in a monotonic millisecond time; incomplete transmissions expire after 10 seconds. The NimBLE example drives retries and timeouts on its event queue without adding an MQTT thread. The credential callback runs only after the reply has been accepted by the transport layer; while waiting, new credential requests are ignored, the pending credentials are not replaced, and no ACK is sent for the new request. Trsmitr out-of-order and timeout diagnostics are emitted through the SDK log facade.

`tuya_ble_prov_cfg_ext_t.random_fn` is optional and provides a CSPRNG that reports the number of bytes filled; short output fails the handshake. When it is not provided, `tuya_ble_hal_random()` must fill the buffer completely.

Configuring a scan provider enables the WiFi-list and status-query capability declarations; the NimBLE example has a provider configured. The port's worker task performs the WiFi scan, copying the original scan token and results through queues, and the BLE execution context delivers them to the SDK. Cancellation does not immediately interrupt the radio scan; until the old task has finished and been handled, no new scan is accepted, so stale results cannot misappropriate a new token. The status query returns only a fixed CFG status; there is no active stage reporting or a complete PSK3.0 activation exchange. Credentials are still received through the legacy token Provisioning flow; completing BLE credential reception does not mean cloud Activation has completed.
