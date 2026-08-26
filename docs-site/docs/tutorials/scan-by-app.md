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
int iot_get_qrcode_info(const iot_qrcode_request_t *request, char *url, size_t url_len);
```

向涂鸦云请求一个用于配网激活的 URL。设备将此 URL 编码为二维码展示给用户。

**`iot_qrcode_request_t` 字段：**

| 字段 | 说明 |
|------|------|
| `uuid` | 设备 UUID |
| `authkey` | 设备 Auth Key |
| `app_id` | App ID（可为空字符串） |
| `type` | 二维码类型（通常为 1） |
| `region` | 数据中心区域（默认 `AY` 中国） |
| `env` | 环境：`PROD` / `PRE` |
| `cacert` / `cert_bundle_attach` | HTTPS/IoT-DNS 的 TLS 证书配置，详见 [TLS 证书验证](../guides/tls-cert-verification.md) |

**返回值：** `OPRT_OK` 表示成功，激活 URL 写入调用方提供的 `url` 缓冲区（NUL 结尾；缓冲区不够大时返回 `OPRT_INVALID_RESULT`）。

### `iot_client_init_on_boarding()`

```c
iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);
```

阻塞等待用户通过 App 扫码完成激活。内部会通过 MQTT 监听激活事件，当 App 扫
码并确认配网后自动完成设备激活。

:::warning 必须连接 MQTT，App 才判定配网成功
**App 只有在检测到设备连接上涂鸦云 MQTT 通道（设备上线）后，才会判定配网成功。**
仅完成激活、拿到 `devid` 等凭据但不连接 MQTT，App 端会显示配网失败/超时。

因此这一点现在由默认行为保证——自动连接是默认开启的，无需额外配置；只要不设 `.mqtt_disable_auto_connect`，设备就会在激活完成后自动连接 MQTT：

```c
iot_on_boarding_config_t ob_config = {
    // ...
    // 不设 .mqtt_disable_auto_connect：默认即自动连接 MQTT，App 才能判定配网成功
};
```

若选择保持 `false`，则必须在激活成功后立即手动调用
`iot_client_connect()`。
:::

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

- 此方式要求设备已具备网络连接能力（Wi-Fi 或以太网），且设备必须在激活后连接涂鸦平台的 MQTT 通道——**App 以设备 MQTT 上线作为配网成功的判定条件**（见上文警告，切勿设 `.mqtt_disable_auto_connect = true`）。
- `iot_client_init_on_boarding()` 会阻塞直到 App 扫码完成或超时
  （`timeout_ms` 配置），实际产品中建议在单独线程中调用。
- 本示例使用 `qrcodegen`（nayuki 库）生成二维码，实际产品可替换为任意
  QR 生成方案。
