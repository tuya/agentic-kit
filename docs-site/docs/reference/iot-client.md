---
title: IoT Client API 参考文档
sidebar_label: IoT Client
sidebar_position: 3
---

# IoT Client API 参考文档

IoT Client 模块（CMake 目标 `tuya_iot_client`，产物 `libtuya_iot_client.a`）提供设备激活、MQTT 连接和会话令牌获取功能。头文件：`modules/iot-client/include/iot_client.h`。

:::caution 前置调用
使用任何 SDK 函数前，必须先调用 [`iot_init()` 或 `iot_init_default()`](#iot_init--iot_init_default) 初始化 PAL。未初始化时 `iot_client_init()` 直接返回 `NULL`，`iot_get_qrcode_info()` 返回 `OPRT_UNINITIALIZED`。
:::

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

### MQTT 状态码（`MQTTStatus_t`）

日志形如 `MQTT_Connect failed: MQTTServerRefused (6)` 中括号里的数字。**现在的日志会同时打出名字**，本表主要用于查阅早期日志和历史工单里只有裸数字的情况。

| 值 | 名称 | 在本 SDK 场景下的含义 |
|----|------|------|
| 0 | `MQTTSuccess` | 成功 |
| 1 | `MQTTBadParameter` | 参数非法（本地问题，不会到达网络）|
| 2 | `MQTTNoMemory` | 缓冲区不足，收发包放不下 |
| 3 | `MQTTSendFailed` | 底层发送失败——网络已断，或 TLS 会话失效 |
| 4 | `MQTTRecvFailed` | 底层接收失败，同上 |
| 5 | `MQTTBadResponse` | 收到了报文但格式非法（对端不是合法 MQTT broker，或链路串包）|
| 6 | `MQTTServerRefused` | **broker 明确拒绝**了 CONNECT 或 SUBSCRIBE——网络完全正常，是业务层不允许。见下表 |
| 7 | `MQTTNoDataAvailable` | 本次没有数据可读，正常轮询结果 |
| 8 | `MQTTIllegalState` | 状态机非法状态 |
| 9 | `MQTTStateCollision` | QoS 报文 ID 冲突 |
| 10 | `MQTTKeepAliveTimeout` | 等待 PINGRESP 超时，链路已死但 socket 未报错 |
| 11 | `MQTTNeedMoreBytes` | 报文不完整，需再次调用（非错误）|

### CONNACK 拒绝原因

`MQTTServerRefused (6)` 只说明「被拒了」，具体哪一种由 broker 在 CONNACK 里给出。一次被拒的连接实际会打出四行，**第一行**就是原因：

```
10:15:13 [E] [mqtt] Connection refused: bad user name or password.
10:15:13 [E] [mqtt] CONNACK recv failed with status = MQTTServerRefused.
10:15:13 [E] [mqtt] MQTT connection failed with status = MQTTServerRefused.
10:15:13 [E] [iot] MQTT_Connect failed: MQTTServerRefused (6)
```

前三行来自 coreMQTT（`[mqtt]`），最后一行来自 SDK（`[iot]`）。时间戳前缀由默认日志处理器加上；若应用用 `log_set_handler()` 换了处理器，前缀形式取决于该实现。

| CONNACK code | 文案 | 常见原因与处理 |
|----|------|------|
| 1 | unacceptable protocol version | broker 不支持 MQTT 3.1.1，基本不会出现 |
| 2 | identifier rejected | clientId 不被接受——检查 devid 是否完整（正常为 20–22 字节）|
| 3 | server unavailable | 云端临时不可用，退避重试即可，与凭据无关 |
| 4 | bad user name or password | 凭据不被认可 |
| 5 | not authorized | 未授权 |

4 和 5 最常见，且**多数情况下不是密码算错**，而是**设备已在云端被解绑/删除**——凭据本身格式与算法都正确，只是服务端不再认这个 `devid`。快速确认方法：拿同一套凭据走 ATOP HTTP 打一个接口（如 `iot_atop_call` 调 `tuya.device.schema.newest.get`），ATOP 会原样带回云端的 `errorCode`，信息量远大于 CONNACK；若同样被拒，即可确认需要重新配网激活。

:::note
`[mqtt]` 前缀的行来自 coreMQTT 内部，由 `common/core_mqtt_config.h` 接入日志门面。若这些行没有出现，说明该构建仍带着 `MQTT_DO_NOT_USE_CUSTOM_CONFIG`，拒绝原因会被丢弃，只剩一个裸的 `6`。
:::

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

### Reset Type（`iot_reset_type_t`）

云端设备移除（protocol 11）通知的分类，由 `reset_callback` 收到。

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `IOT_RESET_REMOTE_UNBIND` | 用户在 App 上移除设备（可重新配网绑定） |
| 1 | `IOT_RESET_REMOTE_FACTORY` | 云端下发恢复出厂设置 |

### Log Level（`log_level_t`）

日志通过 `common/log.h` 的全局日志门面控制，使用 `log_set_level()` 设置运行时级别，使用 `log_set_handler()` 自定义输出。

注：`log_level_t` 并非枚举，而是 `typedef int`（以便 `LOG_*` 可用于预处理器 `#if` 判断），下表中的名称均为宏定义。

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
| `mqtt_disable_auto_connect` | `bool` | `false`（默认）初始化后自动连接 MQTT；`true` 需手动调用 [`iot_client_connect()`](#iot_client_connect) |
| `cacert` | `const char *` | CA 证书 PEM（用于 MQTT/HTTPS/IoT-DNS TLS，调用方持有，需在 client 生命周期内有效） |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（如 ESP-IDF 的 `esp_crt_bundle_attach`），NULL 表示不使用。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT 消息回调，可为 NULL |
| `reset_callback` | `iot_reset_callback_t` | 云端解绑/恢复出厂（protocol 11）通知回调，可为 NULL。注册后 protocol 11 消息由 SDK 消费，不再进入 `message_callback` |
| `reset_user_data` | `void *` | 透传给 `reset_callback` 的用户指针，可为 NULL |
| `ota_confirm_callback` | `iot_ota_confirm_callback_t` | APP 确认 OTA 升级（protocol 15）通知回调，可为 NULL。注册后 protocol 15 消息由 SDK 消费，不再进入 `message_callback` |
| `ota_confirm_user_data` | `void *` | 透传给 `ota_confirm_callback` 的用户指针，可为 NULL |
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
| `mqtt_disable_auto_connect` | `bool` | `false`（默认）激活后自动连接 MQTT；`true` 需手动调用 [`iot_client_connect()`](#iot_client_connect) |
| `cacert` | `const char *` | CA 证书 PEM（用于 MQTT/HTTPS/IoT-DNS TLS，调用方持有） |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（如 ESP-IDF 的 `esp_crt_bundle_attach`），NULL 表示不使用。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |
| `message_callback` | `iot_message_callback_t` | MQTT 消息回调 |
| `reset_callback` | `iot_reset_callback_t` | 云端解绑/恢复出厂（protocol 11）通知回调，可为 NULL。注册后 protocol 11 消息由 SDK 消费，不再进入 `message_callback` |
| `reset_user_data` | `void *` | 透传给 `reset_callback` 的用户指针，可为 NULL |
| `ota_confirm_callback` | `iot_ota_confirm_callback_t` | APP 确认 OTA 升级（protocol 15）通知回调，可为 NULL。注册后 protocol 15 消息由 SDK 消费，不再进入 `message_callback` |
| `ota_confirm_user_data` | `void *` | 透传给 `ota_confirm_callback` 的用户指针，可为 NULL |
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

### `iot_init` / `iot_init_default`

```c
int iot_init(const pal_t *pal);
int iot_init_default(void);
```

初始化 IoT SDK 的平台抽象层（PAL），**必须先于任何其他 SDK 函数调用**。`iot_init_default()` 使用内置的默认 PAL 适配器（POSIX / FreeRTOS）；`iot_init()` 使用自定义 PAL（详见[适配新平台](../guides/porting-to-new-platform.md)）。

**返回值：** `OPRT_OK` 成功；`iot_init()` 在 `pal` 为 NULL 或必需函数指针缺失时返回 `OPRT_INVALID_PARAMETER`。

---

### `iot_client_init`

```c
iot_client_t *iot_client_init(const iot_client_config_t *config);
```

使用已有设备凭据（devid, secret_key, local_key）初始化 IoT 客户端，解析 MQTT/HTTPS 端点。默认会自动建立 MQTT 连接；设 `mqtt_disable_auto_connect = true` 则需手动调用 [`iot_client_connect()`](#iot_client_connect)。**注意**：自动连接失败时 `iot_client_init()` 会释放 client 并返回 `NULL`，所以开机时网络可能未就绪的设备应显式关闭自动连接、自行重连。

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

### `iot_client_reset`

```c
int iot_client_reset(iot_client_t *client, iot_reset_scope_t scope,
                     char *error_code, size_t error_code_len);
```

告知云端本设备正在重置，调用的是 ATOP 接口 `tuya.device.reset`（version `5.0`），请求体固定为：

```json
{"resetFactory":true,"t":1756108800}
```

`resetFactory` 由 `scope` 参数决定，两者**能否撤销完全不同**：

| `scope` | `resetFactory` | 云端行为 | 可逆性 |
|---|---|---|---|
| `IOT_RESET_UNBIND_ONLY` | `false` | 仅解除「用户—设备」绑定，设备的云端数据保留 | 重新配网可以接回原数据 |
| `IOT_RESET_FACTORY` | `true` | 在解绑之外，**清理该设备的全部相关数据**（特殊业务另有约定的除外） | **不可逆** |

这与云端下行 protocol 11 里 `IOT_RESET_REMOTE_UNBIND` / `IOT_RESET_REMOTE_FACTORY` 的区分是同一对语义，只是方向相反（那是云端推给设备的分类，这是设备向云端提出的选择）。

:::danger IOT_RESET_FACTORY 不可逆
`IOT_RESET_FACTORY` 会让云端删除该设备**所有相关数据**，没有任何恢复手段：重新配网得到的是一个新的绑定，不是原来的状态。只在**设备退役 / 交付新用户**时使用。

日常场景（用户在 App 里解绑后设备自清理、或设备换绑）用 `IOT_RESET_UNBIND_ONLY`。
:::

:::caution 两者都不是「重连」
无论哪个 scope 都会交出绑定关系。想干净地重连，用 [`iot_client_disconnect()`](#iot_client_disconnect) + [`iot_client_connect()`](#iot_client_connect)。
:::

**成败决定 client 的归属**——这是使用本接口最重要的一点：

| 返回 | client 状态 | 调用方要做什么 |
|---|---|---|
| `OPRT_OK` | **已销毁**（等同 `iot_client_deinit()` 释放的全部资源） | 不得再使用该指针，不要再 disconnect / deinit |
| 其他任何值 | **完好可用** | 可重试，或自行调用 `iot_client_deinit()` 收尾 |

也就是说返回码回答的是「云端是否已知道」，而不是「client 是否还活着」。失败时不销毁是刻意的：设备本地已解绑、而云端仍认为绑定，是比重试更糟的状态。

两件本接口**不做**的事：

1. **不清理持久化数据。** 凭据、DP 状态、schema 存在哪里只有应用知道，擦除仍归应用（参考 `examples/posix/pair/unbind-demo/`）。
2. **不等待 protocol 11 通知。** 那条推送是「云端/用户从 App 移除设备」的表现；设备主动重置由本调用的返回码确认，不会另外收到通知。

:::warning
不要在 `iot_client_process()` 触发的回调（`message_callback` / `reset_callback` / `ota_confirm_callback`）里调用本函数——它会释放 coreMQTT 接收循环当前栈上仍在使用的 mqtt client。正确做法是置标志位，回到应用主循环再重置。
:::

### 被拒绝时必须看 `error_code`

`OPRT_ATOP_BUSINESS_ERROR` 只说明「云端拒绝了」，而两种拒绝的处理**完全相反**——单看返回码无法区分，这是 `error_code` 出参存在的唯一理由：

| errorCode | 含义 | 正确处理 |
|---|---|---|
| `REMOTE_API_RUN_UNKNOW_FAILED` | 服务器忙 | 退避后重试 |
| `GATEWAY_NOT_EXISTS` | 云端已无此设备（绑定关系本就不在） | **重试永远不会成功**：擦除本地凭据、重新配网 |

把后者当成可重试，设备会永久重试、永远不重新配网——凭据不擦、配网不进，等于报废。

```c
char err[64] = {0};
/* 退役设备用 IOT_RESET_FACTORY；日常解绑用 IOT_RESET_UNBIND_ONLY */
int rc = iot_client_reset(client, IOT_RESET_UNBIND_ONLY, err, sizeof(err));
if (rc == OPRT_OK) {
    /* client 已销毁；擦除自己保存的凭据即可 */
} else if (strcmp(err, "GATEWAY_NOT_EXISTS") == 0) {
    wipe_credentials();          /* 别重试 */
    enter_pairing_mode();
} else {
    /* client 完好，可重试 */
}
```

**参数：**
- `client` — 已激活的 IoT 客户端实例
- `scope` — 清理范围，见上表；除设备退役外一律用 `IOT_RESET_UNBIND_ONLY`
- `error_code` — 可选，接收云端 errorCode；云端未给时为 `""`。不需要可传 `NULL`。`IOT_ATOP_ERROR_CODE_LEN`（48）字节足够
- `error_code_len` — `error_code` 缓冲区大小（传 NULL 时忽略）

**返回值：** `OPRT_OK` 成功（client 已销毁）；`OPRT_INVALID_PARAMETER` client 为 NULL；`OPRT_UNINITIALIZED` 尚无设备凭据（未激活）；`OPRT_ATOP_BUSINESS_ERROR` 云端拒绝（见上表）；其他为传输层错误。

---

### `iot_client_deinit`

```c
void iot_client_deinit(iot_client_t *client);
```

反初始化 IoT 客户端，断开 MQTT 连接，释放所有资源。

---

### `iot_client_connect`

```c
int iot_client_connect(iot_client_t *client);
```

连接 MQTT broker 并订阅设备入站 topic。两种场景需要调用：

1. 初始化/激活时设了 `mqtt_disable_auto_connect = true`，需要自行建链；
2. 链路断开后重连——与 [`iot_client_disconnect()`](#iot_client_disconnect) 配对用在应用自己的重连循环里。

**不会自动重试，也不会自动刷新 CA 证书**：TLS 握手失败会直接返回 `OPRT_TLS_HANDSHAKE_FAILED`。证书恢复由应用负责——收到该错误码后需先用 `iot_get_ca_certificate()` 重新获取并重新赋值 `client->cacert`，再发起重连；否则 broker 证书轮换后，重连循环会对同一个注定失败的握手无限重试。可参考 `examples/posix/dp-management/` 的写法。

**参数：** `client` — IoT 客户端实例（需已设置 `mqtt_url` 和 `devid`）

**返回值：** `OPRT_OK` 成功；`OPRT_INVALID_PARAMETER` 表示 client / URL / devid 缺失；其他为错误码。

---

### `iot_client_disconnect`

```c
void iot_client_disconnect(iot_client_t *client);
```

断开 MQTT 连接并销毁 MQTT 客户端。传 `NULL` 或未连接时是安全的空操作，可重复调用。

:::warning
不要在 `iot_client_process()` 触发的回调（`message_callback` / `reset_callback` / `ota_confirm_callback`）里调用本函数或 `iot_client_deinit()`——两者都会释放 coreMQTT 接收循环当前栈上仍在使用的 mqtt client：回调返回后它还要再次解引用该上下文去回 ack、整理网络缓冲区。正确做法是置标志位，让应用主循环去断开。
:::

**参数：** `client` — IoT 客户端实例（`NULL` 安全）

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

**返回值：** `OPRT_OK` 成功；`client` 为 NULL 时返回 `OPRT_INVALID_PARAMETER`；无 MQTT 连接时返回 `OPRT_UNINITIALIZED`。

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

**返回值：** `OPRT_OK` 成功；`client` 为 NULL 或 `data` 为 NULL / `data_len` 为 0 时返回 `OPRT_INVALID_PARAMETER`；无 MQTT 连接时返回 `OPRT_UNINITIALIZED`；加密缓冲区分配失败返回 `OPRT_MALLOC_FAILED`；加密或发布失败返回 `OPRT_COMMUNICATION_ERROR`。

---

### `iot_get_qrcode_info`

```c
int iot_get_qrcode_info(const iot_qrcode_request_t *request, char *url, size_t url_len);
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

**出参：**
- `url` — 调用方分配的缓冲区，接收以 NUL 结尾的激活 URL
- `url_len` — 缓冲区大小（字节）

**返回值：** `OPRT_OK` 成功；`request`/`url` 为 NULL 或 `url_len` 为 0 时返回 `OPRT_INVALID_PARAMETER`；缓冲区不够大时返回 `OPRT_INVALID_RESULT`。

---

### `iot_get_ca_certificate`

```c
int iot_get_ca_certificate(iot_client_t *client, const char *host, uint16_t port,
                           char *ca_certificate, size_t ca_certificate_len);
```

获取目标主机的 CA 证书。

**参数：**
- `client` — IoT 客户端实例（不可为 NULL）
- `host` — 目标主机名
- `port` — 目标端口
- `ca_certificate` — 调用方分配的缓冲区，接收以 NUL 结尾的 CA 证书 PEM 字符串（单个 CA PEM 通常 1-2KB，建议 4096 字节）
- `ca_certificate_len` — 缓冲区大小（字节）

**返回值：** `OPRT_OK` 成功；`client`/`host`/`ca_certificate` 为 NULL 或 `ca_certificate_len` 为 0 时返回 `OPRT_INVALID_PARAMETER`；无可用证书或缓冲区不够大时返回 `OPRT_INVALID_RESULT`。

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
