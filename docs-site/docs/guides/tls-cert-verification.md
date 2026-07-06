---
title: TLS 证书验证
sidebar_label: TLS 证书验证
sidebar_position: 7
---

# TLS 证书验证

agentic-kit 的所有云端通信（IoT-DNS、ATOP HTTPS、MQTT）默认走 TLS——IoT-DNS 与 ATOP HTTPS 使用端口 443，MQTT over TLS（mqtts，默认 `mqtts://a6.tuyacn.com:8883`）使用端口 8883。如果不在配置中提供任何证书校验信息，连接仍会加密，但**不会校验服务器证书**——存在中间人攻击（MITM）风险。

SDK 提供两种方式启用服务器证书验证：

| 方式 | 适用场景 | 原理 |
|------|---------|------|
| `.cacert`（PEM 字符串） | POSIX（Linux/macOS）、有足够 RAM 存放完整 PEM 的设备 | 传入 CA 根证书的 PEM 文本，SDK 解析后用于 mbedTLS 证书链校验 |
| `.cert_bundle_attach`（平台证书包回调） | ESP-IDF 等 RTOS 平台，RAM 紧张或证书以编译后的二进制形式管理 | 传入平台 SDK 的证书包挂载函数（如 `esp_crt_bundle_attach`），直接操作 mbedTLS 的 `ssl_config` |

两者可以同时不设（向后兼容，退化为不校验），但不建议在生产环境中这样做。

## 验证优先级

当两个字段同时提供时，`.cacert` 优先。底层 `tls_connect()` 的逻辑：

```
1. cacert 非空？            → 用 cacert 解析 PEM，authmode = VERIFY_REQUIRED
2. cert_bundle_attach 非空？ → 调用回调挂载平台证书包，authmode = VERIFY_REQUIRED
3. 两者都空？              → authmode = cfg->verify
```

第 3 步的 `cfg->verify` 由各上层模块设定，不通过 iot-client 的公开配置暴露：**iot-client 固定传 `TLS_VERIFY_NONE`**（即两者都空时不校验、仅加密，并打印一条警告）；而 RTC/TAI 通道传的是 `TLS_VERIFY_OPTIONAL`。所以对 iot-client 用户而言，“两者都空 = 不校验”成立；这也正是生产环境必须至少设置 `.cacert` 或 `.cert_bundle_attach` 之一的原因。

## 涉及的连接

配置生效后，以下**所有**连接都会使用证书验证：

- IoT-DNS 查询（`h1.iot-dns.com:443`）
- ATOP HTTPS 请求（设备元数据、session token、OTA 查询等）
- MQTT 连接（`mqtt_disable_tls = false` 时）

无需为每个连接单独设置——在 `iot_client_config_t` 或 `iot_on_boarding_config_t` 上设置一次即可。

---

## 方式一：`.cacert`（PEM 字符串）

适用于 POSIX 平台或内存充裕、可以直接在代码中嵌入 PEM 文本的场景。

### 用法

```c
/* 根 CA 证书 PEM —— 指向静态字符串或堆分配的 PEM 文本均可，
   但必须在 client 整个生命周期内有效（SDK 不拷贝）。 */
static const char *root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDx...（省略）...\n"
    "-----END CERTIFICATE-----\n";

iot_client_config_t cfg = {
    .devid      = "...",
    .secret_key = "...",
    .local_key  = "...",
    .region     = AY,
    .env        = PROD,
    .cacert     = root_ca,          /* ← 设置 CA 证书 */
};
iot_client_t *client = iot_client_init(&cfg);
```

### 运行时获取 CA 证书

如果不想在固件中硬编码 PEM，可以通过 IoT-DNS 在运行时查询**某个目标端点**的 CA 证书。签名要求传入目标主机名与端口（`host` 不能为 NULL，否则返回 `OPRT_INVALID_PARAMETER`）：

```c
int iot_get_ca_certificate(iot_client_t *client, const char *host,
                           uint16_t port, char **ca_certificate);
```

```c
/* 查询目标端点（例如 ATOP/HTTPS 主机用 443，MQTT 用 8883）的 CA 证书。 */
char *ca_cert = NULL;
int rc = iot_get_ca_certificate(client, "a1.tuyacn.com", 443, &ca_cert);
if (rc == OPRT_OK && ca_cert != NULL) {
    /* ... 使用 / 持久化 ca_cert ... */

    /* ca_cert 由 PAL 分配（pal_strdup），必须用同一个 PAL 的 free 释放，
       不能用 libc free()；且需在 iot_client_deinit() 之前（client->pal 仍有效时）释放。 */
    client->pal->free(ca_cert);
}
```

> **引导阶段的“先有鸡还是先有蛋”问题：** `iot_get_ca_certificate()` 本身要先建立一次到 IoT-DNS 的 TLS 连接；若此时 `client->cacert` 仍为空，这次查询以不校验方式进行——存在引导阶段 MITM 风险。同理，`iot_client_init()` 在返回前就已完成 DNS/ATOP/MQTT 的**首批连接**，因此**在 init 之后再设 `client->cacert` 只对后续请求生效，无法追溯保护 init 期间的连接**。
>
> 所以运行时获取 CA 更适合用来**取回一份 CA 加以持久化**，下次启动前通过 `cfg.cacert` 在 `iot_client_init()` **之前**传入，从第一条连接起即校验；对安全要求严格的场景，建议直接硬编码根 CA 或改用 `.cert_bundle_attach`。

### 内存注意事项

- 完整的 Mozilla CA 证书包约 200KB，在 RAM 紧凑的 MCU 上可能过大。
- 单个 Tuya 云端 CA 证书约 1-2KB，可接受。
- `.cacert` 指向的字符串不拷贝，调用方必须保证它在 `iot_client_deinit()` 之前一直有效。

---

## 方式二：`.cert_bundle_attach`（平台证书包回调）

适用于 ESP-IDF 等 RTOS 平台。平台以编译后的二进制形式管理证书包（存储在 flash 分区或固件镜像中），不需要在 RAM 中持有 PEM 文本。

### 原理

ESP-IDF 提供 `esp_crt_bundle_attach()`，它将一个预编译的 CA 证书包（包含主流公共根 CA）直接挂载到 mbedTLS 的 `ssl_config` 上。SDK 在 TLS 握手前调用这个回调，握手时 mbedTLS 会用证书包中的 CA 校验服务器证书。

### ESP-IDF 用法

```c
#include "esp_crt_bundle.h"
#include "iot_client.h"

iot_client_config_t cfg = {
    .devid      = "...",
    .secret_key = "...",
    .local_key  = "...",
    .region     = AY,
    .env        = PROD,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};
iot_client_t *client = iot_client_init(&cfg);
```

配网（on-boarding）场景同理：

```c
iot_on_boarding_config_t ob_cfg = {
    .uuid       = "...",
    .authkey    = "...",
    .product_key = "...",
    .env        = PROD,
    .mqtt_disable_tls = false,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};
iot_client_t *client = iot_client_init_on_boarding(&ob_cfg);
```

### ESP-IDF sdkconfig 配置

确保在 `sdkconfig` 中启用了证书包：

```ini
# 启用 mbedTLS 证书包（默认包含主流公共根 CA）
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

### 类型转换说明

`esp_crt_bundle_attach` 的签名是 `esp_err_t (*)(void *conf)`，而 SDK 定义的 `tls_cert_bundle_attach_fn` 是 `void (*)(void *ssl_config)`。两者签名不完全一致，需要通过 `(tls_cert_bundle_attach_fn)` 强制转换。这是安全的——回调的语义是"向 `mbedTLS_ssl_config` 挂载证书"，ESP-IDF 的实现与 mbedTLS 的 `mbedtls_ssl_conf_verify` / CA 链操作兼容。

### RTC/TAI 连接（AI 对话通道）

RTC TCP Client 同样支持 `cert_bundle_attach`，设置方式与 iot-client 一致：

```c
tai_config_t tai_cfg = {
    .host              = cp.host,
    .port              = cp.port,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
    /* ... 其他字段 ... */
};
```

这样 iot-client 和 RTC/TAI 两个模块使用同一个证书包，所有云端连接都经过验证。

### 其他 RTOS 平台

如果目标平台不是 ESP-IDF，但提供了类似的证书包机制，可以实现一个签名匹配 `tls_cert_bundle_attach_fn` 的回调函数，在函数内将平台的证书包挂载到传入的 `mbedTLS_ssl_config *` 上。

---

## 哪些 API 受影响

以下公开 API 的配置结构体均支持 `.cacert` 和 `.cert_bundle_attach`：

| API | 配置结构体 | 说明 |
|-----|-----------|------|
| `iot_client_init` | `iot_client_config_t` | 已激活设备初始化（DNS + ATOP + MQTT） |
| `iot_client_init_on_boarding` | `iot_on_boarding_config_t` | App 扫码激活 |
| `iot_client_init_on_boarding_with_token` | `iot_on_boarding_config_t` | Token 激活 |
| `iot_get_qrcode_info` | `iot_qrcode_request_t` | 获取配网二维码 URL（独立 API，不需要 client） |

配置后，这些 API 内部发起的所有 TLS 连接都会使用证书验证。

---

## 如何确认验证是否生效

### 查看日志

SDK 在 TLS 握手时会输出日志。启用证书验证时：

- **`cacert` 生效**：无警告日志，TLS 握手成功后输出 `[tls] connected ...`
- **`cert_bundle_attach` 生效**：同上，无 "verification disabled" 警告
- **两者都未设置**：会输出警告（`LOG_WARN` 级别）：
  ```
  [tls] peer verification disabled (no CA certificate)
  ```

如果看到上述警告，说明该连接未启用证书验证。

### 验证失败的错误

当证书验证启用但服务器证书不受信任时，TLS 握手会失败。MQTT / ATOP HTTPS 路径返回 `OPRT_TLS_HANDSHAKE_FAILED`（-7）；而 **IoT-DNS 查询路径会把 TLS 失败归一为 `OPRT_COMMUNICATION_ERROR`（-1）**，因此排查 DNS/CA 获取阶段的失败时不要只匹配 -7。常见原因：

- 服务器证书链无法追溯到所提供的 CA
- 证书包中缺少涂鸦云的根 CA
- 系统时间不正确（证书有 Not Before / Not After 时间窗口）

---

## 最佳实践

1. **生产环境必须启用证书验证。** 无论是 `.cacert` 还是 `.cert_bundle_attach`，至少设置一个。
2. **ESP-IDF 平台优先使用 `.cert_bundle_attach`。** 不占用 RAM 存放 PEM，证书包存储在 flash，支持主流公共 CA。
3. **POSIX 平台使用 `.cacert`。** 从 IoT-DNS 动态获取或硬编码根 CA PEM。
4. **不要在生产环境留空两个字段。** 连接虽加密但不校验，存在 MITM 风险。
5. **确保系统时间正确。** 证书有有效期，时间偏差过大会导致验证失败。MCU 平台应在联网后通过 NTP 同步时间。
