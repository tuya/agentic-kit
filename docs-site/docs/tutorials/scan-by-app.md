---
title: App 扫码配网
sidebar_label: App 扫码配网
sidebar_position: 5
---

# App 扫码配网

> 对应示例：`examples/posix/pair/scan-by-app/`

本章介绍另一种配网方式：**设备端生成并展示二维码，用户使用涂鸦 App 扫码完成
配网激活**。

:::note 前置条件
在阅读本章之前，请确保你已具备：
- **产品 PID** 和 **设备授权码**（uuid + authkey）
  → 详见[创建和配置 Agent](../guides/create-agent)
:::

在上一章（[设备扫码配网](./scan-by-device)）中，配网流程是 App 显示二维码、设备用摄像头扫码。
但在部分产品形态中，设备可能没有摄像头（例如带屏音箱、智能面板），却有屏幕
可以显示二维码。此时可以反过来——设备端向涂鸦云请求一个激活 URL，将其编码为
二维码展示在屏幕（或终端）上，用户用涂鸦 App 扫描该二维码即可完成配网。

激活成功后，设备同样会获得 `devid`、`secret_key`、`local_key`，后续使用方
式与扫码配网完全一致。

## 整体流程

```
iot_get_qrcode_info()               // 1. 向涂鸦云请求激活 URL
        |
        v
qrcodegen_encodeText()              // 2. 将 URL 编码为二维码
print_qr_terminal()                 //    在终端/屏幕上显示
        |
        v
iot_client_init_on_boarding()        // 3. 等待 App 扫码，完成激活
        |
        v
iot_client_get_session_token()       // 4. 验证云端连通性
        |
        v
iot_client_deinit()                  // 5. 清理资源
```

## 关键 API

### `iot_get_qrcode_info()`

```c
int iot_get_qrcode_info(const iot_qrcode_request_t *request,
                        iot_qrcode_response_t *response);
```

向涂鸦云请求一个用于配网激活的 URL。设备将此 URL 编码为二维码展示给用户。

**`iot_qrcode_request_t` 字段：**

| 字段 | 说明 |
|------|------|
| `uuid` | 设备 UUID |
| `authkey` | 设备 Auth Key |
| `app_id` | App ID（可为空字符串） |
| `type` | 二维码类型（通常为 1） |
| `env` | 环境：`PROD` / `PRE` |

**返回值：** `OPRT_OK` 表示成功，`response->url` 中包含激活 URL（调用方需 `free`）。

### `iot_client_init_on_boarding()`

```c
iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);
```

阻塞等待用户通过 App 扫码完成激活。内部会通过 MQTT 监听激活事件，当 App 扫
码并确认配网后自动完成设备激活。

**与 `iot_client_init_on_boarding_with_token()` 的区别：**

- `init_on_boarding()` — 不需要预知 Token，通过 MQTT 等待 App 扫码触发激活
- `init_on_boarding_with_token()` — 需要已知 Token（从二维码解析或 OpenAPI 获取），直接发起激活

## 运行示例

```sh
# 二维码模式：设备展示二维码，等待 App 扫码
./build/scan_by_app_pair_demo

# Token 模式：直接使用 Token 激活（当前实现仍会先请求并打印二维码 URL，便于调试）
./build/scan_by_app_pair_demo <token>
```

## 与"设备扫码"方式的对比

| | 设备扫码（[scan-by-device](./scan-by-device)） | App 扫码（本章） |
|---|---|---|
| 二维码由谁生成 | App 生成 | 设备生成 |
| 二维码由谁扫描 | 设备（摄像头） | 用户（App） |
| 设备硬件要求 | 需要摄像头 | 需要屏幕或终端输出 |
| 二维码内容 | WiFi 凭据 + Token（JSON） | 涂鸦云激活 URL |
| 激活方式 | `init_on_boarding_with_token()` | `init_on_boarding()` |
| 网络信息传递 | 通过二维码传递 WiFi 信息 | 设备需自行联网 |

## 注意事项

- 此方式要求设备已具备网络连接能力（Wi-Fi 或以太网），在 APP 扫码配网过程中，设备需与涂鸦平台建立 MQTT 连接。
- `iot_client_init_on_boarding()` 会阻塞直到 App 扫码完成或超时
  （`timeout_ms` 配置），实际产品中建议在单独线程中调用。
- 本示例使用 `qrcodegen`（nayuki 库）生成二维码，实际产品可替换为任意
  QR 生成方案。
