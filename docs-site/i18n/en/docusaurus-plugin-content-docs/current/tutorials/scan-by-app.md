---
title: App QR Code Provisioning
sidebar_label: App QR Code Provisioning
sidebar_position: 5
---

# App QR Code Provisioning

> Corresponding example: `examples/posix/pair/scan-by-app/`

This chapter describes another Provisioning method: **the device generates and displays a QR code, and the user scans it
with the Tuya App to complete Provisioning and Activation**.

:::note Prerequisites
Before reading this chapter, make sure you have:
- **Product PID** and **device authorization code** (uuid + authkey)
  → see [Create and configure an Agent](../guides/create-agent)
:::

In the previous chapter ([Device QR Code Provisioning](./scan-by-device)), the Provisioning flow has the App display a QR
code and the device scan it with a camera. But in some product forms the device may have no camera (for example, a
screen-equipped smart speaker or a smart panel), yet has a screen that can display a QR code. In that case the roles can
be reversed—the device requests an Activation URL from the Tuya cloud, encodes it as a QR code, and displays it on the
screen (or terminal); the user then scans that QR code with the Tuya App to complete Provisioning.

After Activation succeeds, the device likewise obtains `devid`, `secret_key`, and `local_key`, and the subsequent usage
is exactly the same as QR code Provisioning.

## Overall flow {#整体流程}

```
iot_get_qrcode_info()               // 1. Request an Activation URL from the Tuya cloud
        |
        v
qrcodegen_encodeText()              // 2. Encode the URL as a QR code
print_qr_terminal()                 //    Display it in the terminal / on the screen
        |
        v
iot_client_init_on_boarding()        // 3. Wait for the App to scan the QR code and complete Activation
        |
        v
iot_client_get_session_token()       // 4. Verify cloud connectivity
        |
        v
iot_client_deinit()                  // 5. Release resources
```

## Key APIs {#关键-api}

### `iot_get_qrcode_info()` {#iot_get_qrcode_info}

```c
int iot_get_qrcode_info(const iot_qrcode_request_t *request, char *url, size_t url_len);
```

Requests a URL from the Tuya cloud for Provisioning and Activation. The device encodes this URL as a QR code and
displays it to the user.

**`iot_qrcode_request_t` fields:**

| Field | Description |
|------|------|
| `uuid` | Device UUID |
| `authkey` | Device Auth Key |
| `app_id` | App ID (may be an empty string) |
| `type` | QR code type (usually 1) |
| `region` | Data center region (defaults to `AY` China) |
| `env` | Environment: `PROD` / `PRE` |
| `cacert` / `cert_bundle_attach` | TLS certificate configuration for HTTPS/IoT-DNS, see [TLS certificate verification](../guides/tls-cert-verification.md) |

**Return value:** `OPRT_OK` indicates success and the Activation URL is written into the `url` buffer provided by the caller (NUL-terminated; returns `OPRT_INVALID_RESULT` if the buffer is too small).

### `iot_client_init_on_boarding()` {#iot_client_init_on_boarding}

```c
iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);
```

Blocks while waiting for the user to complete Activation by scanning the QR code with the App. Internally it listens for
the Activation event over MQTT, and automatically completes device Activation once the App scans the QR code and confirms
Provisioning.

:::warning MQTT must be connected for the App to judge Provisioning successful
**The App only considers Provisioning successful after it detects that the device has connected to the Tuya cloud MQTT channel (the device is online).**
If you only complete Activation and obtain credentials such as `devid` without connecting to MQTT, the App shows Provisioning failed / timed out.

This is therefore now guaranteed by the default behavior—auto-connect is enabled by default with no extra configuration required; as long as you do not set `.mqtt_disable_auto_connect`, the device connects to MQTT automatically after Activation completes:

```c
iot_on_boarding_config_t ob_config = {
    // ...
    // Do not set .mqtt_disable_auto_connect: it auto-connects to MQTT by default, which is what lets the App judge Provisioning successful
};
```

Only if the application explicitly sets `.mqtt_disable_auto_connect = true` must it call `iot_client_connect()` manually immediately after Activation succeeds.
:::

**Differences from `iot_client_init_on_boarding_with_token()`:**

- `init_on_boarding()` — does not require knowing the Token in advance; waits over MQTT for the App scan to trigger Activation
- `init_on_boarding_with_token()` — requires a known Token (parsed from a QR code or obtained via OpenAPI) and initiates Activation directly

## Running the example {#运行示例}

```sh
# QR code mode: the device displays a QR code and waits for the App to scan it
./build/scan_by_app_pair_demo

# Token mode: activate directly with a Token (the current implementation still requests and prints the QR code URL first, for easier debugging)
./build/scan_by_app_pair_demo <token>
```

## Comparison with the "device QR scan" method {#与设备扫码方式的对比}

| | Device QR scan ([scan-by-device](./scan-by-device)) | App QR scan (this chapter) |
|---|---|---|
| Who generates the QR code | The App | The device |
| Who scans the QR code | The device (camera) | The user (App) |
| Device hardware requirements | Camera required | Screen or terminal output required |
| QR code content | WiFi credentials + Token (JSON) | Tuya cloud Activation URL |
| Activation method | `init_on_boarding_with_token()` | `init_on_boarding()` |
| Network information delivery | WiFi information is delivered through the QR code | The device must connect to the network itself |

## Notes {#注意事项}

- This method requires the device to already have network connectivity (Wi-Fi or Ethernet), and the device must connect to the Tuya platform's MQTT channel after Activation—**the App uses the device coming online on MQTT as the criterion for judging Provisioning successful** (see the warning above; never set `.mqtt_disable_auto_connect = true`).
- `iot_client_init_on_boarding()` blocks until the App scan completes or the timeout is reached
  (configured by `timeout_ms`); in a real product, calling it on a separate thread is recommended.
- This example uses `qrcodegen` (the nayuki library) to generate the QR code; a real product can replace it with any
  QR generation scheme.
