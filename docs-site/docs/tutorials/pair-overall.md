---
title: 配网方式总览
sidebar_label: 配网方式总览
sidebar_position: 1
---

# 配网方式总览

新设备首次使用前，需要通过配网操作将设备与涂鸦云的 App 账号绑定，并激活授权码（对应架构图中 IoT 通信模块）。当前支持的配网方式：

| | 设备扫 App 二维码 | 设备展示二维码 App 扫 | OpenAPI Token 激活 | BLE 蓝牙配网 |
|---|---|---|---|---|
| 依赖 App | 是 | 是 | **否** | 是 |
| 设备硬件要求 | 摄像头 | 屏幕 | 无特殊要求 | BLE |
| Token 来源 | App 二维码中 | 云端 MQTT 推送 | OpenAPI 返回 | App BLE 传递 |
| 网络信息传递 | 二维码含 WiFi 凭据 | 设备需自行联网 | 设备需自行联网 | BLE 传递 WiFi 凭据 |
| 适用场景 | 带摄像头的设备 | 带屏设备 | 无 App / 不用 Tuya App SDK 的 App | 支持 BLE 的设备 |
| 对应教程 | [设备扫码配网](./scan-by-device) | [App 扫码配网](./scan-by-app) | [OpenAPI 配网](./openapi-activate) | [BLE 配网](./pair-by-ble) |

激活成功后，云端会为设备分配 `devid`、`secret_key`、`local_key`，后续使用 AI SDK 时均需使用这三个字段。

:::warning App 配网的成功判定：设备必须上线 MQTT
对于依赖 App 的配网方式（设备扫码、App 扫码、BLE 配网），**App 只有在检测到设备成功连接涂鸦云 MQTT 通道（设备上线）后，才会判定配网成功**。仅完成激活、拿到 `devid` 等凭据但不连接 MQTT，App 端会显示配网失败/超时——即使设备侧的激活请求本身已经返回成功。

因此，配网时务必将配置中的 `.mqtt_auto_connect` 设为 `true`（默认为 `false`），让设备在激活完成后自动连接 MQTT；或在激活成功后立即手动调用 `iot_client_message_connect()`。
:::

注：以上说的 App，可以是以下任意一种：

* Tuya App（或 Smart Life App）
* 客户基于 Tuya App 进行 OEM 的 App（零开发）
* 基于 Tuya App SDK 开发的 App（开发能力强，有差异化需求）
