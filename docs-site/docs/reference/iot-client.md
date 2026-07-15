---
title: IoT Client API 参考文档
sidebar_label: IoT Client
sidebar_position: 3
---

# IoT Client API 参考文档

`libiot_sdk` 提供设备激活、MQTT 连接和会话令牌获取功能。头文件：`modules/iot-client/include/iot_client.h`。

## 激活请求补充说明

设备激活时，底层会调用 `atop_activate_request()` 向 `thing.device.opensdk.active` 发送请求。该接口使用 `activite_request_t` 组织请求参数，其中 `options` 字段会按入参动态拼接。

- 当 `sdk_version` 非空时，`options` 会包含 `sdkFullVer`
- `otaChannel` 固定为 `0`
- `isFK` 会根据 `firmware_key` 是否存在在 `true` / `false` 间切换
- 当传入 `firmware_key` 时，还会额外上送 `productKeyStr`

示例：

```json
{"otaChannel":0,"sdkFullVer":"agentic-kit_0.1.0","isFK":false}
```

`sdkFullVer` 的默认来源是 `SDK_VERSION` 宏，当前在 on-boarding 流程中会写入 `activite_request_t.sdk_version`；如果调用方自行构造 `activite_request_t`，也可以显式传入其他版本值。相关定义可参考源码 `modules/iot-client/src/atop.h` 与 `modules/iot-client/src/atop.c`。

## 错误码

| 值 | 宏 | 说明 |
|----|-----|------|
| 0 | `OPRT_OK` | 执行成功 |
| -1 | `OPRT_COMMUNICATION_ERROR` | 通信错误 |
| -2 | `OPRT_INVALID_PARAMETER` | 无效参数 |
| -3 | `OPRT_INVALID_RESULT` | 无效结果 |
| -4 | `OPRT_UNINITIALIZED` | 未初始化 |
| -5 | `OPRT_NOT_SUPPORTED` | 不支持 |
| -6 | `OPRT_MALLOC_FAILED` | 内存分配失败 |
| -7 | `OPRT_TLS_HANDSHAKE_FAILED` | TLS 握手失败 |

## 枚举类型

### Region（`iot_region_t`）

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `AY` | 中国（上海）|
| 1 | `AZ` | 美国西部（俄勒冈） |
| 2 | `UEAZ` | 美国东部（佛吉尼亚） |
| 3 | `EU` | 欧洲（法兰克福） |
| 4 | `WEAZ` | 欧洲西部（荷兰埃姆斯哈文） |
| 5 | `IN` | 印度（孟买） |
| 6 | `SG` | 东南亚（新加坡） |

注:如果设备用Tuya智能或智能生活配网,目前无法支持美国东部或欧洲西部数据中心。 用API
配网方式可以支持。

### Environment（`iot_env_t`）

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `PROD` | 生产环境 |
| 1 | `PRE` | 预发布环境 |
| 2 | `TEST` | 测试环境 |

### Log Level（`log_level_t`）

日志通过 `common/log.h` 的全局日志门面控制，使用 `log_set_level()` 设置运行时级别，使用 `log_set_handler()` 自定义输出。

| 值 | 名称 |
|----|------|
| 0 | `LOG_NONE` |
| 1 | `LOG_ERROR` |
| 2 | `LOG_WARN` |
| 3 | `LOG_INFO` |
| 4 | `LOG_DEBUG` |

## 配置结构体

### `iot_client_config_t`

用于已激活设备的初始化配置。

| 字段 | 类型 | 说明 |
|------|------|------|
| `devid` | `char[32]` | 设备 ID |
| `secret_key` | `char[32]` | 设备密钥 |
| `local_key` | `char[32]` | 本地加密密钥 |
| `region` | `iot_region_t` | 数据中心区域 |
| `env` | `iot_env_t` | 环境 |
| `mqtt_disable_tls` | `bool` | `false`（默认）使用 MQTTS，`true` 使用明文 MQTT |
| `mqtt_auto_connect` | `bool` | `true` 初始化后自动连接 MQTT；`false`（默认）需手动调用 |
| `cacert` | `const char *` | CA 证书 PEM（用于 MQTT/HTTPS/IoT-DNS TLS，调用方持有，需在 client 生命周期内有效） |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（如 ESP-IDF 的 `esp_crt_bundle_attach`），NULL 表示不使用。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT 消息回调，可为 NULL |
| `schema` | `const char *` | 重启时用于恢复的 DP schema JSON（调用方持有，NULL = 不恢复 / 宽松模式） |
| `schema_id` | `const char *` | 持久化的 schema id（schema 升级查询的稳定 key，可为 NULL） |
| `dp_state` | `const char *` | 持久化的 DP 当前状态 `{"dps":{...}}`，用于恢复（不置脏、不上报，可为 NULL） |
| `sw_ver` | `const char *` | 应用固件版本号（如 `"1.2.3"`），`iot_client_init` 时自动上报供云端 OTA 比较；NULL 表示使用 SDK 默认 `IOT_SDK_SW_VER`。详见 [OTA 升级](../guides/ota-upgrade.md) |

### `iot_on_boarding_config_t`

用于设备配网激活的配置。

| 字段 | 类型 | 说明 |
|------|------|------|
| `uuid` | `char[32]` | 设备 UUID（从涂鸦平台申请的授权码） |
| `authkey` | `char[64]` | Auth Key |
| `product_key` | `char[32]` | 产品 PID |
| `firmware_key` | `char[64]` | 固件 Key（可为空） |
| `modules` | `const char *` | 模块信息（可为 NULL） |
| `feature` | `const char *` | Feature 信息（可为 NULL） |
| `skill_param` | `const char *` | Skill 参数（可为 NULL） |
| `timeout_ms` | `int` | 激活超时时间（毫秒） |
| `env` | `iot_env_t` | 环境：`PROD`（默认）或 `PRE` |
| `mqtt_disable_tls` | `bool` | TLS 开关 |
| `mqtt_auto_connect` | `bool` | `true` 激活后自动连接 MQTT；`false`（默认）需手动调用 |
| `cacert` | `const char *` | CA 证书 PEM（用于 MQTT/HTTPS/IoT-DNS TLS，调用方持有） |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（如 ESP-IDF 的 `esp_crt_bundle_attach`），NULL 表示不使用。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT 消息回调 |
| `sw_ver` | `const char *` | 应用固件版本号（如 `"1.2.3"`），激活后自动上报供云端 OTA 比较；NULL 表示使用 SDK 默认 `IOT_SDK_SW_VER`。详见 [OTA 升级](../guides/ota-upgrade.md) |

### `iot_client_t`（返回实例）

由 `iot_client_init()` 或配网 API 返回的客户端实例，包含以下关键字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `devid` | `char[32]` | 激活后分配的设备 ID |
| `secret_key` | `char[32]` | MQTT 认证密钥 |
| `local_key` | `char[32]` | 本地加密密钥 |
| `region` | `iot_region_t` | 服务器区域 |
| `env` | `iot_env_t` | 环境 |

## API 函数

### `iot_client_init`

```c
iot_client_t *iot_client_init(const iot_client_config_t *config);
```

使用已有设备凭据（devid, secret_key, local_key）初始化 IoT 客户端，解析 MQTT/HTTPS 端点。当 `mqtt_auto_connect` 为 `true` 时自动建立 MQTT 连接，否则需手动调用连接。

**返回值：** 成功返回 `iot_client_t *`；失败返回 `NULL`。

---

### `iot_client_init_on_boarding`

```c
iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config);
```

阻塞等待 App 扫码激活。内部通过 MQTT 监听激活事件，激活成功后返回包含 `devid`、`secret_key`、`local_key` 的客户端实例。

**返回值：** 成功返回 `iot_client_t *`；超时或失败返回 `NULL`。

---

### `iot_client_init_on_boarding_with_token`

```c
iot_client_t *iot_client_init_on_boarding_with_token(
    const iot_on_boarding_config_t *config,
    const char *token);
```

使用预知的激活 Token 直接发起激活请求，跳过 MQTT 等待。Region 由 token 前两个字符自动推导。

**参数：**
- `config` — 配网配置
- `token` — 激活 Token（格式：`{region}{token}{secret}`，如 `AYH73H8u7Ap4pX`）

**返回值：** 成功返回 `iot_client_t *`；失败返回 `NULL`。

---

### `iot_client_deinit`

```c
void iot_client_deinit(iot_client_t *client);
```

反初始化 IoT 客户端，断开 MQTT 连接，释放所有资源。

---

### `iot_client_get_session_token`

```c
int iot_client_get_session_token(iot_client_t *client, const char *agent_code, char *token, size_t token_len);
```

从涂鸦云获取 AI 会话令牌（session_token），用于创建 STM Open SDK 会话。

**参数：**
- `client` — IoT 客户端实例
- `agent_code` — Agent 代码（传 `NULL` 使用默认 Agent）
- `token` — 输出缓冲区，接收 session token 字符串
- `token_len` — 输出缓冲区大小（字节）

**返回值：** `OPRT_OK` 成功；其他为错误码。

---

### `iot_client_process`

```c
int iot_client_process(iot_client_t *client, uint32_t timeout_ms);
```

处理 MQTT 事件（接收消息、维持心跳）。在需要接收 MQTT 消息的场景下，应在循环中调用此函数。

**参数：**
- `client` — IoT 客户端实例
- `timeout_ms` — 处理超时时间（毫秒）

**返回值：** `OPRT_OK` 成功。

---

### `iot_client_publish`

```c
int iot_client_publish(iot_client_t *client, const uint8_t *data, size_t data_len);
```

向 `smart/device/out/{deviceid}` 发布加密消息。

**参数：**
- `client` — IoT 客户端实例
- `data` — 明文数据（内部自动加密）
- `data_len` — 数据长度

**返回值：** `OPRT_OK` 成功。

---

### `iot_get_qrcode_info`

```c
int iot_get_qrcode_info(const iot_qrcode_request_t *request,
                        iot_qrcode_response_t *response);
```

从涂鸦云获取配网激活 URL，设备可将此 URL 编码为二维码展示给用户。

**请求参数 `iot_qrcode_request_t`：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `uuid` | `const char *` | 设备 UUID |
| `authkey` | `const char *` | Auth Key |
| `app_id` | `const char *` | App ID（可为空字符串） |
| `type` | `int` | 二维码类型（通常为 1） |
| `region` | `iot_region_t` | 数据中心区域 |
| `env` | `iot_env_t` | 环境 |
| `cacert` | `const char *` | CA 证书 PEM（用于 HTTPS/IoT-DNS TLS，调用方持有） |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（如 ESP-IDF 的 `esp_crt_bundle_attach`），NULL 表示不使用。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |

**响应 `iot_qrcode_response_t`：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `url` | `char *` | 激活 URL（调用方需 `free`） |

**返回值：** `OPRT_OK` 成功。

---

### `iot_get_ca_certificate`

```c
int iot_get_ca_certificate(iot_client_t *client, const char *host,
                           uint16_t port, char **ca_certificate);
```

获取目标主机的 CA 证书。

**参数：**
- `client` — IoT 客户端实例（不可为 NULL）
- `host` — 目标主机名
- `port` — 目标端口
- `ca_certificate` — 输出 CA 证书字符串（调用方需 `free`）

**返回值：** `OPRT_OK` 成功；`client` 或 `host` 为 NULL 时返回 `OPRT_INVALID_PARAMETER`。

---

### 日志配置

IoT Client 使用 `common/log.h` 提供的全局日志门面，不再提供单独的日志回调设置 API。

```c
#include "log.h"

// 设置运行时日志级别
log_set_level(LOG_INFO);

// 自定义日志输出处理函数
log_set_handler(my_log_handler);
```
