---
title: Provisioning Methods Overview
sidebar_label: Provisioning Methods Overview
sidebar_position: 1
---

# Provisioning Methods Overview

Before a new device can be used for the first time, it must be bound to a Tuya cloud App account through a Provisioning operation and have its authorization code activated (corresponding to the IoT communication module in the architecture diagram). The Provisioning methods currently supported are:

| | Device scans the App's QR code | Device shows a QR code for the App to scan | OpenAPI Token Activation | BLE Provisioning |
|---|---|---|---|---|
| Requires App | Yes | Yes | **No** | Yes |
| Device hardware requirements | Camera | Screen | No special requirements | BLE |
| Token source | In the App's QR code | Pushed by the cloud over MQTT | Returned by OpenAPI | Passed by the App over BLE |
| Network information delivery | The QR code contains WiFi credentials | The device must connect to the network itself | The device must connect to the network itself | WiFi credentials passed over BLE |
| Use cases | Devices with a camera | Devices with a screen | No App / Apps that do not use the Tuya App SDK | Devices that support BLE |
| Corresponding tutorial | [Device QR Code Provisioning](./scan-by-device) | [App QR Code Provisioning](./scan-by-app) | [OpenAPI Provisioning](./openapi-activate) | [BLE Provisioning](./pair-by-ble) |

After Activation succeeds, the cloud assigns the device a `devid`, `secret_key`, and `local_key`; all three fields are required when using the AI SDK later.

:::warning How App Provisioning is judged successful: the device must come online on MQTT
For Provisioning methods that depend on the App (device QR scan, App QR scan, and BLE Provisioning), **the App only considers Provisioning successful after it detects that the device has successfully connected to the Tuya cloud MQTT channel (the device is online)**. If you only complete Activation and obtain credentials such as `devid` without connecting to MQTT, the App shows Provisioning failed / timed out—even if the device-side Activation request itself has already returned success.

Provisioning depends on this, and auto-connect is the default behavior with no extra configuration required; if you explicitly set `.mqtt_disable_auto_connect = true`, you must call `iot_client_connect()` manually immediately after Activation succeeds.
:::

Note: The App referred to above can be any of the following:

* Tuya App (or Smart Life App)
* An App OEM'd by the customer from the Tuya App (zero development)
* An App developed based on the Tuya App SDK (strong development capability, with differentiation needs)
