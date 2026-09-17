---
title: OpenAPI Provisioning
sidebar_label: OpenAPI Provisioning
sidebar_position: 6
---

# OpenAPI Provisioning

> Corresponding example: `examples/posix/pair/api-activate/`

:::tip
OpenAPI Provisioning requires creating a new cloud project and associating an App. First follow [Create an App and a cloud project](../guides/create-cloud-project.md) to complete the cloud-side configuration.
:::

This chapter introduces the third Provisioning method: **without relying on the Tuya App, use the Tuya OpenAPI (Cloud API) to create the user and generate the Provisioning token on the server side, then pass the token to the device to complete Activation**.

The first two Provisioning methods both require users to install and use the Tuya App to complete scan-based Provisioning. However, in some scenarios, device manufacturers may:

- Have their own App or backend system and do not want to depend on the Tuya App
- Have devices with neither a screen nor a camera

In this case, you can use the Tuya OpenAPI to synchronize the user and generate the Provisioning token directly on the server side, then pass the token to the device by any means (serial port, Bluetooth, HTTP, etc.). The device then calls `iot_client_init_on_boarding_with_token()` to complete Activation.

## Overall flow {#整体流程}

```
[Server / script]                                 [Device]
      |                                                    |
      |  1. POST /v1.0/apps/{schema}/user                  |
      |     (create/sync user, obtain uid)                 |
      |                                                    |
      |  2. POST /v1.0/device/paring/token                 |
      |     (generate Provisioning token, region, secret)  |
      |                                                    |
      |  3. Concatenate {region}{token}{secret}            |
      |     Pass to device ---------------------->         |
      |                                                    |
      |                        4. iot_client_init_on_boarding_with_token()
      |                          (activate the device with the token)
      |                                                    |
      |  5. GET /v1.0/device/paring/tokens/{token}         |
      |     (poll to confirm the Provisioning result)      |
```

## OpenAPIs involved {#涉及的-openapi}

### 1. User sync — `POST /v1.0/apps/{schema}/user` {#1-用户同步--post-v10appsschemauser}

Create or synchronize a user in the Tuya cloud; returns the user's `uid`.

| Parameter | Type | Required | Description |
|------|------|------|------|
| `country_code` | string | Yes | Country code, e.g. `"86"` |
| `username` | string | Yes | Username |
| `password` | string | Yes | Password (MD5 hash) |
| `username_type` | int | Yes | 1 = mobile number, 2 = email, 3 = other |

### 2. Generate a Provisioning token — `POST /v1.0/device/paring/token` {#2-生成配网-token--post-v10deviceparingtoken}

| Parameter | Type | Required | Description |
|------|------|------|------|
| `uid` | string | Yes | User uid |
| `paring_type` | string | Yes | Provisioning type: `BLE`, `AP`, `EZ` |
| `time_zone_id` | string | Yes | Time zone |

### 3. Query the Provisioning result — `GET /v1.0/device/paring/tokens/{token}` {#3-查询配网结果--get-v10deviceparingtokenstoken}

Query whether the device has completed Activation with this token.

### 4. Device reset (restore factory defaults) — `POST /v2.0/cloud/thing/{device_id}/reset` {#4-设备重置恢复出厂设置--post-v20cloudthingdevice_idreset}

Unbind the device and restore it to factory defaults based on the device ID. After a reset, the binding between the device and the original user is cleared, and the device must go through the Provisioning and Activation flow again before it can be used.

| Parameter | Type | Location | Required | Description |
|------|------|------|------|------|
| `device_id` | string | path | Yes | Device ID |

Return example:

```json
{
    "tid": "b8a2b49abbbc11eda71e169efc83a172",
    "result": true,
    "t": 1678065474602,
    "success": true
}
```

After the device receives the reset notification from the cloud, it should clear the locally stored Activation information (uuid, authkey, etc.) and return to the pending-Provisioning state.

> Reference: [Restore Factory Defaults](https://developer.tuya.com/en/docs/cloud/187da401c3?id=Kcp5pqcrmyo21)

## Token format {#token-格式}

The token the device receives must be concatenated from three parts:

```
{region}{token}{secret}
```

For example: `region="AY"`, `token="H73H8u7A"`, `secret="p4pX"` → the complete token is `AYH73H8u7Ap4pX`.

The device-side `iot_client_init_on_boarding_with_token()` automatically parses the Region information from the first two characters.

## Environment configuration {#环境配置}

| Environment variable | Description | Example |
|----------|------|------|
| `TUYA_CLIENT_ID` | Tuya IoT Platform Access ID | `xwm........` |
| `TUYA_CLIENT_SECRET` | Tuya IoT Platform Access Secret | `95f13d4a616d407a...` |
| `TUYA_BASE_URL` | OpenAPI endpoint | `https://openapi.tuyacn.com` |
| `SCHEMA` | App schema identifier (used by the shell script; the Python script also supports the `TUYA_SCHEMA` or `--schema` argument) | `marsid` |

`TUYA_BASE_URL` for each data center:

| Data center | URL |
|----------|-----|
| China | `https://openapi.tuyacn.com` |
| US West | `https://openapi.tuyaus.com` |
| Europe | `https://openapi.tuyaeu.com` |
| India | `https://openapi.tuyain.com` |

## Running the example {#运行示例}

```sh
export TUYA_CLIENT_ID="your_client_id"
export TUYA_CLIENT_SECRET="your_secret"
export TUYA_BASE_URL="https://openapi.tuyacn.com"
export SCHEMA="your_schema"

# One-shot script
./build/activate-demo.sh

# Or run step by step
python3 ./build/tuya_openapi.py sync-user --schema marsid --country-code 86 \
    --username "test_user" --password "mypassword" --username-type 3

python3 ./build/tuya_openapi.py pairing-token --uid "ay..." --paring-type BLE \
    --time-zone-id "Asia/Shanghai"

./build/activate_demo "$PAIRING_TOKEN" <uuid> <authkey> <product_key>
```

## Notes {#注意事项}

- The Provisioning token has an expiry; the device must complete Activation before it expires. You can poll for the result with `tuya_openapi.py pairing-result --poll` (`--timeout` defaults to 100 seconds).
- `tuya_openapi.py` is implemented with the Python standard library and requires no extra dependencies.
- In a real product, OpenAPI calls should be made in the manufacturer's own backend service; **do not expose the Access Secret on the client or the device**.
