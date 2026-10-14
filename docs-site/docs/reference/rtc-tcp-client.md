---
title: RTC TCP Client SDK 参考文档
sidebar_label: RTC TCP Client
sidebar_position: 1
---

# RTC TCP Client SDK 参考文档

## 1. 概述 {#1-概述}

**RTC TCP Client**（API 前缀 `tai_*`）是 tRTC(tuya自研RTC协议) 的 TCP 实现。以源码形式提供，通过 PAL（Platform Abstraction Layer）实现跨平台移植，适用于 POSIX、FreeRTOS（ESP-IDF）等环境。

头文件：`tuya_ai.h`（单一 include）

### 特点 {#特点}

- **简洁 API**：类型化发送函数（`tai_send_text`、`tai_send_audio_*`、`tai_send_image`），无需手动组装数据结构体
- **后台接收线程**：`tai_connect` 后自动启动后台线程处理接收和 keepalive
- **回调驱动**：通过 `on_audio`、`on_text`、`on_image`、`on_event`、`on_disconnect` 接收数据
- **无外部依赖**：用户 PAL 只需提供原始 TCP 和平台原语；TLS 和加密由 SDK 内部通过 bundled mbedTLS 处理


---

## 2. 常量定义 {#2-常量定义}

### 2.1 数据包类型 {#21-数据包类型}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_PKT_CLIENT_HELLO` | 1 | 客户端握手 |
| `TAI_PKT_AUTHENTICATE_RESPONSE` | 3 | 服务端对 ClientHello 的鉴权结果 |
| `TAI_PKT_PING` | 4 | 心跳请求 |
| `TAI_PKT_PONG` | 5 | 心跳响应 |
| `TAI_PKT_CONNECTION_CLOSE` | 6 | 连接关闭 |
| `TAI_PKT_SESSION_NEW` | 7 | 新建会话 |
| `TAI_PKT_SESSION_CLOSE` | 8 | 关闭会话 |
| `TAI_PKT_CONNECTION_REFRESH_REQ` | 9 | 连接刷新请求 |
| `TAI_PKT_CONNECTION_REFRESH_RESP` | 10 | 连接刷新响应 |
| `TAI_PKT_VIDEO` | 30 | 视频数据 |
| `TAI_PKT_AUDIO` | 31 | 音频数据 |
| `TAI_PKT_IMAGE` | 32 | 图像数据 |
| `TAI_PKT_FILE` | 33 | 文件数据 |
| `TAI_PKT_TEXT` | 34 | 文本数据 |
| `TAI_PKT_EVENT` | 35 | 事件数据 |

### 2.2 流标志（Stream Flags） {#22-流标志stream-flags}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_STREAM_ONE_SHOT` | 0x00 | 单包（完整数据） |
| `TAI_STREAM_START` | 0x01 | 流开始 |
| `TAI_STREAM_MIDDLE` | 0x02 | 流中间 |
| `TAI_STREAM_END` | 0x03 | 流结束 |

### 2.3 事件类型 {#23-事件类型}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_EVT_START` | 0 | 会话开始 |
| `TAI_EVT_PAYLOADS_END` | 1 | 负载结束 |
| `TAI_EVT_END` | 2 | 会话结束 |
| `TAI_EVT_ONE_SHOT` | 3 | 单次事件 |
| `TAI_EVT_CHAT_BREAK` | 4 | 聊天打断/云端 VAD 回合结束（用户停止说话或中途插话）；当前云端以此作为回合边界信号；只需清除下行 TTS 缓存/播放队列，不要结束上行 Event |
| `TAI_EVT_SERVER_VAD` | 5 | 云端 VAD 检测到用户停止说话（旧服务端信号，**当前云端不再下发**，常量仅为协议兼容保留） |
| `TAI_EVT_MCP_CMD` | 1000 | MCP 命令（设备侧执行） |
| `TAI_EVT_SERVER_TIMEOVER` | 1001 | 服务端超时 |
| `TAI_EVT_UPDATE_CONTEXT` | 1002 | 上下文更新 |

### 2.4 客户端类型 {#24-客户端类型}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_CLIENT_DEVICE` | 1 | 设备端 |
| `TAI_CLIENT_APP` | 2 | 应用端 |

### 2.5 音频编码 {#25-音频编码}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_AUDIO_PCM` | 101 | 原始 PCM |
| `TAI_AUDIO_OPUS` | 111 | Opus 编码 |

### 2.6 图像格式 {#26-图像格式}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_IMG_JPEG` | 1 | JPEG |
| `TAI_IMG_PNG` | 2 | PNG |

### 2.7 图像负载类型 {#27-图像负载类型}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_IMG_PAYLOAD_RAW` | 0 | 原始二进制 |
| `TAI_IMG_PAYLOAD_BASE64` | 1 | Base64 编码 |
| `TAI_IMG_PAYLOAD_URL` | 2 | URL 字符串 |

### 2.8 签名级别 {#28-签名级别}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_SIGN_NONE` | 0 | 不签名 |
| `TAI_SIGN_HMAC_SHA1` | 1 | HMAC-SHA1 |
| `TAI_SIGN_HMAC_SHA256` | 2 | HMAC-SHA256（推荐） |

### 2.9 数据 ID {#29-数据-id}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_DATA_ID_AUDIO_UP` | 1 | 上行音频 |
| `TAI_DATA_ID_AUDIO_DOWN` | 2 | 下行音频 |
| `TAI_DATA_ID_TEXT_UP` | 3 | 上行文本 |
| `TAI_DATA_ID_TEXT_DOWN` | 4 | 下行文本 |
| `TAI_DATA_ID_IMAGE_UP` | 5 | 上行图像 |
| `TAI_DATA_ID_IMAGE_DOWN` | 6 | 下行图像（配置密钥的 Session 声明） |
| `TAI_DATA_ID_AUDIO_AUX` | 7 | 辅助音频 |

### 2.10 返回码 {#210-返回码}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_OK` | 0 | 成功 |
| `TAI_ERR_ARGS` | -1 | 参数错误 |
| `TAI_ERR_MEM` | -2 | 内存不足 |
| `TAI_ERR_NET` | -3 | 网络错误 |
| `TAI_ERR_TLS` | -4 | TLS 错误 |
| `TAI_ERR_PROTO` | -5 | 协议错误 |
| `TAI_ERR_HMAC` | -6 | HMAC 校验失败 |
| `TAI_ERR_AGAIN` | -7 | 需要重试 |
| `TAI_ERR_CRYPTO` | -8 | 加解密错误 |

---

## 3. 配置（`tai_config_t`） {#3-配置tai_config_t}

调用 `tai_ctx_init` 前填充此结构体。除 `image_key` 外，所有指针字段在 context 生命周期内必须保持有效；该密钥在构造成功时被复制。

### 3.1 服务器配置 {#31-服务器配置}

| 字段 | 类型 | 说明 |
|------|------|------|
| `host` | `const char *` | 服务器地址（通常从 `iot_client_get_session_token()` 返回的 token 中解析得到） |
| `port` | `uint16_t` | 服务器端口 |
| `tls_sni` | `const char *` | TLS SNI 主机名（通常与 host 相同；若 token 中提供域名，优先使用域名） |

### 3.2 身份配置 {#32-身份配置}

| 字段 | 类型 | 说明 |
|------|------|------|
| `device_id` | `const char *` | 设备 ID（配网后获得的 devid） |
| `local_key` | `const char *` | 本地密钥（配网后获得的 local_key） |
| `client_type` | `uint8_t` | 客户端类型：`TAI_CLIENT_DEVICE` 或 `TAI_CLIENT_APP`（0 = 默认 `TAI_CLIENT_DEVICE`） |
| `protocol_version` | `uint8_t` | 协议版本：使用 `TAI_VER_21`（0 = 默认 `TAI_VER_21`） |

### 3.3 会话选项 {#33-会话选项}

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_attrs_json` | `const char *` | 会话属性 JSON（NULL 使用默认值）。可配置云端 VAD、音频格式等 |
| `event_user_data_json` | `const char *` | 事件用户数据 JSON（NULL 使用默认值） |
| `agent_token` | `const char *` | Agent Token（指定特定 Agent，NULL 使用产品默认 Agent） |

### 3.4 业务标识 {#34-业务标识}

| 字段 | 类型 | 说明 |
|------|------|------|
| `biz_code` | `uint32_t` | 业务码（0 = 默认 `65537`） |
| `biz_tag` | `uint64_t` | 业务标签（0 = 默认 `119`） |

### 3.5 安全配置 {#35-安全配置}

| 字段 | 类型 | 说明 |
|------|------|------|
| `sign_level` | `uint8_t` | 未配置 `image_key` 时：`TAI_SIGN_HMAC_SHA256`（推荐）或 `TAI_SIGN_HMAC_SHA1`；0 视为未设置并回退到 HMAC-SHA256。配置 `image_key` 时忽略此字段，使用签名级别 0，所有 Frame 均不附加签名。 |

#### 图像密钥与加密媒体连接 {#image-decrypt-key}

为兼容已有链接，保留历史锚点 `image-decrypt-key`；配置字段名为 `image_key`，同一密钥用于上行加密和下行解密。

| 字段 | 类型 | 说明 |
|------|------|------|
| `image_key` | `const uint8_t *` | 可选的固定 AES-128 图像密钥：恰好 `TAI_IMAGE_KEY_LEN`（16）个解码后的二进制字节。NULL 保持旧有行为。 |

:::warning 配置密钥后必须验证 TLS 对端
`image_key` 启用 TuyaOpen 加密媒体配置：Frame 版本 3、签名级别 0（忽略 `cfg.sign_level`），Attribute 仍使用 v2 布局。ClientHello 的 36 字节 suite 为 `00 || random[32] || 04 03 03`；后三字节分别表示 AES-128-GCM、完整视频 I 帧、完整图像的加密范围，**不是媒体能力位图**。本 SDK 不支持视频或文件处理。

此 unsigned Frame 安全决策已获用户明确批准。图像 GCM 不使用 AAD，只逐个认证完整 Image Packet 的加密正文；元数据、控制 Packet、URL 和空标记依赖经过验证的 TLS 保护。必须配置非空 `cfg.ca_cert` 或 `cert_bundle_attach`；配置密钥时若设置 `disable_tls` 或缺少两种信任来源，`tai_connect()` 在网络操作前返回 `TAI_ERR_TLS`。实现已启用，但不代表已通过真实云端验证。
:::

配置密钥的 Session 仅声明上行音频/文本/图像 ID 1/3/5，下行音频/文本/图像 ID 2/4/6。TuyaOpen 按配置类型顺序分配偶数接收 ID；这支持本 SDK 的 Session 分配，并不意味着协议对所有媒体类型规定了通用固定 ID。`protocol_version` 保持 0 或 `TAI_VER_21`，不要为 Frame 版本 3 将该字段设为 3。

- 传入二进制密钥，不是 Base64 文本、C 字符串、`local_key`、设备凭据或派生的 Frame 密钥。指针必须指向至少 16 个可读字节；应用必须验证解码长度。全零密钥在结构上有效，同样被视为已配置。
- `tai_ctx_init()` 仅在成功时复制密钥，随后调用方可以清零/释放临时字节。context 拥有不可变副本，断连和连接失败均不清除它；拥有者通过 `tai_disconnect()` 停止并 join worker 后，`tai_ctx_deinit()` 会清零该副本。
- 密钥是固定的。获取和可选的保留由应用负责；IoT 初始化及所有 TAI 生命周期/接收路径均不自动获取、重试、刷新、轮换或持久化密钥。重新连接复用同一密钥，无需再次调用 ATOP。
- SDK 和使用公开 `tai_config_t` / `tai_image_msg_t` 布局的调用方必须使用相同头文件重新编译。不透明 context 的内存大小通过 `tai_ctx_size()` 获取。IoT 公开布局（`iot_client_config_t`、`iot_on_boarding_config_t`、`iot_client_t`）不变。

[应用显式获取密钥](#application-image-key) 展示了仅构造、不连接的示例。

### 3.6 保活配置 {#36-保活配置}

| 字段 | 类型 | 说明 |
|------|------|------|
| `ping_interval_ms` | `uint32_t` | Ping 间隔（0 = 默认 60000ms） |
| `ping_timeout_ms` | `uint32_t` | 接收存活超时，任意入站数据均刷新（0 = 默认 90000ms）；主动背压暂停期间不计超时，恢复时获得完整的新预算 |
| `connect_timeout_ms` | `uint32_t` | 连接超时（0 = 默认 5000ms）。分别约束 `tai_connect` 的两个串行等待阶段：先是连接建立（TCP 建连 + TLS 握手，共用一份预算），再是服务端 SessionNew 应答。任一阶段超时即判定连接失败，因此 `tai_connect` 最坏耗时约为该值的 2 倍。 |

### 3.7 测试配置 {#37-测试配置}

| 字段 | 类型 | 说明 |
|------|------|------|
| `disable_tls` | `uint8_t` | 非零时跳过 TLS，仅用于无图像密钥的集成测试；配置密钥时 `tai_connect()` 在网络操作前返回 `TAI_ERR_TLS` |

### 3.8 平台适配 {#38-平台适配}

| 字段 | 类型 | 说明 |
|------|------|------|
| `pal` | `const pal_t *` | 平台适配层实现指针 |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | 平台证书包回调（ESP-IDF 上设为 `(tls_cert_bundle_attach_fn)esp_crt_bundle_attach`）；NULL 表示不使用证书包。可替代 `ca_cert` 提供对端验证。详见 [TLS 证书验证](../guides/tls-cert-verification.md) |
| `ca_cert` | `const char *` | 可选 CA 证书，完整 PEM 或裸 Base64 正文，**不是文件路径**。借用至 context 销毁；非空时要求验证对端。配置图像密钥时必须提供此字段或证书包；无密钥且两者均省略时保留旧有可选验证路径。 |

### 3.9 回调函数 {#39-回调函数}

所有回调均在后台接收线程中调用。

| 字段 | 类型 | 说明 |
|------|------|------|
| `on_audio` | function pointer | 音频数据回调 |
| `on_text` | function pointer | 文本数据回调 |
| `on_image` | function pointer | 图像数据回调（云端生成的图片） |
| `on_event` | function pointer | 事件回调（MCP、打断、VAD 等） |
| `on_disconnect` | function pointer | 断连回调 |
| `on_flow_control` | `int (*)(tai_ctx_t *, void *)` | 可选接收背压钩子；非零允许接收，0 暂停读取与解析，NULL 不启用背压 |
| `user_data` | `void *` | 透传到所有回调 |

**回调签名：**

每个回调接收**单个** `const` 消息结构体指针 + `user_data`（不再是旧的多参数形式）。

```c
void (*on_audio)     (tai_ctx_t *ctx, const tai_audio_msg_t      *msg, void *user_data);
void (*on_text)      (tai_ctx_t *ctx, const tai_text_msg_t       *msg, void *user_data);
void (*on_image)     (tai_ctx_t *ctx, const tai_image_msg_t      *msg, void *user_data);
void (*on_event)     (tai_ctx_t *ctx, const tai_event_msg_t      *msg, void *user_data);
void (*on_disconnect)(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user_data);
int  (*on_flow_control)(tai_ctx_t *ctx, void *user_data);
```

#### 接收背压（`on_flow_control`）

- worker 在每次读取前及完整 Frame 之间调用钩子。符合规范的服务端不会在握手阶段触发它（应答先于媒体下发）；仅当服务端把媒体排在应答之前才可能在握手期被调用。钩子必须非阻塞；应用负责同步共享的队列状态。
- 返回 0 会暂停读取和解析，所有入站流量都会停滞，包括 ChatBreak、ASR 文本、Pong 和 EOF 检测。恢复后先处理已缓冲的完整 Frame，再读取；不完整输入回到有上限的阻塞接收。
- 主动暂停挂起接收存活超时；恢复时获得新的完整 `ping_timeout_ms` 预算。Ping 和停止请求仍执行，Ping 发送失败仍断连。
- 背压也在 Audio Packet 内的每个编解码帧回调前检查。中途暂停会以零拷贝方式保留该 Packet 的剩余字节，恢复时先交付剩余帧、再处理后续 Packet。应用应在 `on_audio` 中按服务端时间（`msg->timestamp_ms`）丢弃过期音频，而不是改写 SDK 接收状态。

默认每 `AGENTIC_KIT_TAI_FLOW_CONTROL_POLL_MS`（50 ms）重查背压，并以 `AGENTIC_KIT_TAI_WORKER_YIELD_MS`（10 ms）在持续流量下让出 CPU。

`pal_t` 新增必填的 `sleep_ms` 回调。它必须非忙等地休眠至少请求的毫秒数，0 为 no-op，且不能依赖 socket 可读性。所有自定义 PAL 和使用方必须用新头文件重新编译。

### 接收消息结构体 {#接收消息结构体}

`tai_audio_msg_t`（音频回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `data` | `const uint8_t *` | Opus 帧 / PCM 字节 |
| `len` | `size_t` | 数据字节数 |
| `codec` | `uint8_t` | `TAI_AUDIO_OPUS` / `TAI_AUDIO_PCM` / 0=未知 |
| `sample_rate` | `uint32_t` | 采样率（Hz），0=未知 |
| `frame_duration` | `uint16_t` | 每 Opus 帧时长（ms） |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*`（取自媒体头） |
| `data_id` | `uint16_t` | 数据 ID：`AUDIO_DOWN`(2) / `AUDIO_AUX`(7) |
| `event_id` | `const char *` | turn id（借用）；无则为 `""` |
| `timestamp_ms` | `uint64_t` | 服务端媒体头时间戳，**非**本地时间；过滤流时以 START 的值锁定，与打断时间比较 |

`tai_text_msg_t`（文本回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `text` | `const char *` | UTF-8 文本，**非** NUL 结尾 |
| `len` | `size_t` | 文本字节数 |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | 数据 ID：`TAI_DATA_ID_TEXT_DOWN`(4) |
| `seq` | `uint32_t` | 每事件内文本序号（varint） |
| `event_id` | `const char *` | turn id（借用）；无则为 `""` |

`tai_image_msg_t`（图像回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `data` | `const uint8_t *` | 借用的图像负载：配置密钥时为认证后的明文字节或 URL 透传；未配置时为旧有原始负载；仅回调期间有效 |
| `payload_type` | `uint8_t` | 配置密钥时：解密后为 `TAI_IMG_PAYLOAD_RAW`，URL 透传为 `TAI_IMG_PAYLOAD_URL`；旧有路径报告该 Packet 解析出的类型，缺省为 RAW |
| `len` | `size_t` | 数据字节数 |
| `format` | `uint8_t` | `TAI_IMG_JPEG` / `TAI_IMG_PNG` / 0=未知 |
| `width` | `uint16_t` | 宽（px），未知或该 Packet 无 image-params 时为 0 |
| `height` | `uint16_t` | 高（px），未知或该 Packet 无 image-params 时为 0 |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | 数据 ID（取自媒体头）；配置密钥的接收路径要求 `TAI_DATA_ID_IMAGE_DOWN`（6） |
| `event_id` | `const char *` | turn id（借用）；无则为 `""` |
| `timestamp_ms` | `uint64_t` | 流起始时间戳（媒体头） |

:::note 流式接收
接收到的图片以分片流形式到达：START（或 ONE_SHOT）携带首字节与 image-params，MIDDLE 继续，END 结束（len 可能为 0）。调用方按 `stream_flag` 复制/累积分片；仅在应用确定累计字节构成受支持的图像文件时，才可在 END 或 ONE_SHOT 后进行图像编解码。`format`/`width`/`height` 来自该 Packet 的 image-params，省略该属性的 MIDDLE/END 为 0。
:::

回调签名和 worker 线程交付不变，新增 `payload_type` 区分交付的负载。已实现的配置密钥路径在 transport Fragmentation 重组后逐个认证完整 Image Packet：24 字节头部含 16 字节 IV；RAW 的正文是密文加 16 字节 GCM tag，Base64 则先严格解码再认证；AES-128-GCM 不使用 AAD。仅成功认证后交付非空图像明文；tag-only 正文也必须认证。24 字节的 header-only START/ONE_SHOT/END 被当作零长度标记，header-only MIDDLE 被拒绝；标记不是认证后的图像数据。URL 只以 URL 类型透传，不进行 GCM 或下载。无密钥路径保持原始字节，不自动解码或解密；SDK 不将 JPEG/PNG 解码为像素。

配置密钥的解析严格校验 image-params 的四个无符号字段及范围、重复 image-params 和 Event ID；START/ONE_SHOT 必须携带 image-params，MIDDLE/END 必须匹配已锁定的 Event/Data ID，若重复携带参数则必须一致。缺失属性的 continuation 继承负载类型，`format`/`width`/`height` 为 0；携带一致属性时报告这些值。孤立或冲突 continuation、畸形 Base64、截断和认证失败均不交付失败 Packet。完成、Event 边界、SessionClose、断连及错误会清除相应流状态，不会触发密钥获取或明文回退。

明文来自临时 PAL 堆缓冲区，回调返回后清零并释放；需要保留时按 `len` 复制。URL/文本不保证 NUL 结尾。现有 `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` 默认 32,000 字节限制单个重组 Packet，而非整张图像。RAW 需要独立明文 scratch，Base64 还需要解码 scratch；云端最大 Packet 和目标峰值内存**尚未实测**。配置密钥的连接已启用，须满足[经过验证的 TLS 要求](#image-decrypt-key)。

`tai_event_msg_t`（事件回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `event_type` | `uint16_t` | `TAI_EVT_*` |
| `data` | `const uint8_t *` | 事件负载（通常为 JSON） |
| `len` | `size_t` | 负载字节数 |
| `event_id` | `const char *` | attr 61（借用）；无则为 `""` |
| `user_data` | `const uint8_t *` | attr 111（借用），**非** NUL 结尾；无则为 NULL |
| `user_data_len` | `size_t` | `user_data` 字节数；与事件负载分离，SDK 不解析其 JSON |

:::note ChatBreak 打断时间
`TAI_EVT_CHAT_BREAK` 的服务端时间不在事件负载里，而在 attr 111 中：`{"breakAttributes":{"time":"<server-time>"}}`，值为服务端 epoch 毫秒，与 `on_audio` 的 `timestamp_ms` 同钟同单位。读取 `msg->user_data`、解析 `breakAttributes.time`，再与 `on_audio` 中锁定的 `timestamp_ms` 比较，即可判定该流是否已过期。MQTT 侧的 `asrInterrupt` 走另一条路径：时间在其自身负载的 `time` 字段，且是同一个服务端时间值。
:::

`tai_disconnect_msg_t`（断连回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `reason` | `uint8_t` | `TAI_DISCONNECT_*`（见下表） |
| `close_code` | `uint16_t` | 服务端关闭码（SESSION/CONNECTION）；其他情况为 0 |
| `detail` | `uint8_t` | `TAI_TRANSPORT_*` / `TAI_PROTO_ERR_*`（见下表）；其他情况为 0 |
| `connection_alive` | `uint8_t` | 仅当 `reason==SESSION_CLOSE` 时为 1 |
| `session_id` | `char[64]` | 值拷贝；无则为 `""` |

:::warning 生命周期
`msg` 以及其内部所有指针（`data` / `text` / `event_id` 等）**仅在回调执行期间有效**——图像明文可能指向临时堆缓冲区，回调返回后即清零并释放。如需在回调返回后保留，请自行拷贝。
:::

#### 断连原因（`reason`） {#断连原因reason}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_DISCONNECT_SESSION_CLOSE` | 0 | 服务端 SessionClose；链路可能仍存活（`connection_alive==1`） |
| `TAI_DISCONNECT_CONNECTION_CLOSE` | 1 | 服务端 ConnectionClose；worker 停止 |
| `TAI_DISCONNECT_TRANSPORT` | 2 | worker 检测到传输层故障 |
| `TAI_DISCONNECT_PROTOCOL` | 3 | fail-fast：解析/行为错误 |

#### `detail`（当 `reason==TRANSPORT`） {#detail当-reasontransport}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_TRANSPORT_PING_TIMEOUT` | 1 | Ping 超时 |
| `TAI_TRANSPORT_EOF` | 2 | 对端关闭（EOF） |
| `TAI_TRANSPORT_NET_ERROR` | 3 | 网络错误 |

#### `detail`（当 `reason==PROTOCOL`） {#detail当-reasonprotocol}

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_PROTO_ERR_BAD_VERSION` | 1 | 未知帧首字节（错位） |
| `TAI_PROTO_ERR_HMAC` | 2 | 帧 HMAC 校验失败 |
| `TAI_PROTO_ERR_FRAME_DECODE` | 3 | 帧头解码失败 |
| `TAI_PROTO_ERR_FRAG` | 4 | 孤立 MIDDLE/LAST、溢出、超大 |
| `TAI_PROTO_ERR_PKT_DECODE` | 5 | 应用包 / 属性块格式错误 |
| `TAI_PROTO_ERR_UNKNOWN_PKT` | 6 | 未知包类型（严格模式） |
| `TAI_PROTO_ERR_EVENT` | 7 | 事件解包失败 / 未知事件类型 |
| `TAI_PROTO_ERR_MEDIA_HDR` | 8 | 媒体/文本头截断 |
| `TAI_PROTO_ERR_UNEXPECTED` | 9 | 包合法但状态错误（行为错误） |
| `TAI_PROTO_ERR_OVERSIZED` | 10 | 入站帧超出 rx_buf |
| `TAI_PROTO_ERR_IMAGE_AUTH` | 11 | 图像 GCM 认证失败 |
| `TAI_PROTO_ERR_IMAGE_PROCESSING` | 12 | 图像 scratch 分配或处理失败 |

这些是正数 fatal detail，不是负数 `TAI_ERR_*` 返回码。图像错误通过一次终止性的 `on_disconnect(PROTOCOL, detail)` 报告；畸形头部/属性、流状态错误仍使用对应的既有 detail。失败 Packet 不触发 `on_image`，应用须丢弃此前累计但尚未完成的图像。

---

## 4. 生命周期 API {#4-生命周期-api}

### `tai_ctx_size` {#tai_ctx_size}

```c
size_t tai_ctx_size(void);
```

返回 context 所需的内存大小。调用方需分配至少此大小的内存，传给 `tai_ctx_init`。

---

### `tai_ctx_init` {#tai_ctx_init}

```c
tai_ctx_t *tai_ctx_init(void *mem, const tai_config_t *cfg);
```

初始化 context。`mem` 必须至少为 `tai_ctx_size()` 字节，且在 context 生命周期内保持有效。

**参数：**
- `mem` — 预分配的内存块
- `cfg` — 配置结构体

**返回值：** 成功返回 `tai_ctx_t*`（即 `mem` 强转），失败返回 NULL。

---

### `tai_ctx_deinit` {#tai_ctx_deinit}

```c
void tai_ctx_deinit(tai_ctx_t *ctx);
```

反初始化 context，释放内部资源。调用后 `mem` 可以 free。必须在 `tai_disconnect` 之后调用。

---

### `tai_connect` {#tai_connect}

```c
int tai_connect(tai_ctx_t *ctx);
```

执行 TLS 握手、发送 ClientHello、确认会话应答并启动后台接收线程。构造时传入 `image_key` 会选择[加密媒体配置](#image-decrypt-key)，而不是阻止连接；若启用 raw TCP 或缺少非空 `ca_cert` / `cert_bundle_attach` 信任来源，则在网络操作前返回 `TAI_ERR_TLS`。TLS 证书或握手失败也返回 `TAI_ERR_TLS`。无密钥时保留旧有行为。

**返回值：** `TAI_OK` 成功，`TAI_ERR_*` 失败。

**行为：**
- 阻塞直到连接建立或失败
- 成功后，后台线程开始处理 Ping/Pong 和接收数据
- 连接成功后即可调用 `tai_send_*` 系列函数

---

### `tai_disconnect` {#tai_disconnect}

```c
void tai_disconnect(tai_ctx_t *ctx);
```

先停止并 join 后台接收线程，再发送一个 best-effort 的 SessionClose，最后关闭传输（TLS / TCP）。

**行为：**
- 阻塞直到后台线程退出（join）
- 调用后不可再发送数据
- 之后应调用 `tai_ctx_deinit`
- **不可**在回调中调用（会 join 自身所在的 worker 线程，导致自死锁）；如需从回调中触发断连，请用 `tai_request_disconnect`

---

### `tai_request_disconnect` {#tai_request_disconnect}

```c
void tai_request_disconnect(tai_ctx_t *ctx);
```

请求后台 worker 停止，但**不** join、**不**拆除连接。可从**任意**线程调用，包括从接收回调内部调用（与 `tai_disconnect` 不同）。worker 会在下一轮循环退出。

**用途：** 在回调中安全地触发断连。调用后，拥有 context 的线程仍需调用 `tai_disconnect()` 来 join worker、发送 SessionClose 并释放资源。

---

## 5. 发送 API {#5-发送-api}

### `tai_send_text` {#tai_send_text}

```c
int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len);
```

发送文本消息。一次调用内部依次发出 4 个应用包：`EventStart` → 文本（`ONE_SHOT` 标志）→ `EventPayloadsEnd` → `EventEnd`，调用方无需再手动收尾。

**参数：**
- `text` — UTF-8 文本
- `len` — 文本字节数

**返回值：** `TAI_OK` 或 `TAI_ERR_*`。

---

### `tai_send_audio_start` {#tai_send_audio_start}

```c
int tai_send_audio_start(tai_ctx_t *ctx,
                         uint8_t codec, uint8_t channels,
                         uint8_t bit_depth, uint32_t sample_rate);
```

开始一段音频流。必须在 `tai_send_audio_chunk` 之前调用。

:::note 云端 VAD 模式
启用云端 VAD（Server VAD，连续对话）时，整个聊天会话只调用一次本函数，不要每个回合重复调用；上行音频流保持打开直到会话结束。详见 [VAD 与打断](../guides/vad-and-interrupt.md) 中的"云端 VAD vs 设备端 VAD"对比表。
:::

**参数：**
- `codec` — 编码格式：`TAI_AUDIO_PCM`（101）或 `TAI_AUDIO_OPUS`（111）
- `channels` — 声道数（通常为 1）
- `bit_depth` — 位深（通常为 16）
- `sample_rate` — 采样率（通常为 16000）

---

### `tai_send_audio_chunk` {#tai_send_audio_chunk}

```c
int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len);
```

发送一帧音频数据。可多次调用（流中间包）。

**参数：**
- `pcm` — 音频数据（PCM 或 Opus 帧，取决于 `audio_start` 时的 codec）
- `len` — 数据字节数

---

### `tai_send_audio_end` {#tai_send_audio_end}

```c
int tai_send_audio_end(tai_ctx_t *ctx);
```

结束音频流。通知服务端本次音频输入完毕，开始处理。

:::warning 云端 VAD 模式下不要调用
启用云端 VAD（`asr.enableVad`）时**不应**调用本函数。当前云端的回合结束信号是 `TAI_EVT_CHAT_BREAK`（`TAI_EVT_SERVER_VAD` 已不再下发），收到它也不要调用本函数。它仅用于手动按键对讲或设备端本地 VAD 判停。错误调用会主动结束当前上行 Event，导致云端截断用户语音。详见 [VAD 与打断](../guides/vad-and-interrupt.md)。
:::

---

### `tai_send_image` {#tai_send_image}

```c
int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height);
```

发送单张图片。

`image_key` 同时用于三个图像发送 API：`tai_send_image()`、`tai_send_image_with_text()` 和 `tai_send_image_audio_start()`。加密准备在 EventStart 之前完成，不修改调用方缓冲区；非空图片需要 `len + 32` 字节临时 scratch（16 字节 IV + `len` 字节密文 + 16 字节 tag），空图片需要 16 字节 scratch（仅 IV，无 tag）。scratch 在发送完成或失败后清零并释放；无密钥发送保持旧有行为。这些发送路径使用[经过验证的 TLS 加密媒体连接](#image-decrypt-key)；真实云端兼容性仍待验证。

**参数：**
- `data` — 图片二进制数据
- `len` — 数据长度
- `format` — 图片格式：`TAI_IMG_JPEG`（1）或 `TAI_IMG_PNG`（2）
- `width` / `height` — 图片尺寸

---

### `tai_send_image_with_text` {#tai_send_image_with_text}

```c
int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height);
```

同时发送文本和图片（如图片理解场景）。内部会先发文本包再发图片包，组成一次完整请求。

---

### `tai_chat_break` {#tai_chat_break}

```c
int tai_chat_break(tai_ctx_t *ctx);
```

发送聊天打断事件。用于用户主动打断 AI 回复（如按键中断）。服务端收到后会停止当前响应生成。

---

### `tai_send_mcp_response` {#tai_send_mcp_response}

```c
int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response);
```

发送 MCP 命令的响应。当 `on_event` 收到 `TAI_EVT_MCP_CMD` 时，设备执行完命令后通过此函数返回结果。

**参数：**
- `json_rpc_response` — JSON-RPC 格式的响应字符串

---

## 6. 日志 {#6-日志}

SDK 日志的全局开关是编译期的 `AGENTIC_KIT_LOG_LEVEL`（在 `common/log.h` 统一设限，作用于整个 SDK，不限于本模块）；本模块可再用 `AGENTIC_KIT_TAI_LOG_LEVEL` 单独压低（原 `TAI_LOG_LEVEL` 的改名，只降不升，默认等于全局值）。有效值：

| 值 | 含义 |
|----|------|
| 0 | 全关 |
| 1 | `TAI_LOG_ERROR` |
| 2 | `TAI_LOG_WARN` |
| 3 | `TAI_LOG_INFO` |
| 4 | `TAI_LOG_DEBUG`（默认） |

高于上限的日志在编译期消除；上限以下无条件输出——没有运行时级别（`tai_set_log_level()` 已随运行时层一起移除）。量产用 `-DAGENTIC_KIT_LOG_LEVEL=2` 压到 error + warn，或用 `-DAGENTIC_KIT_TAI_LOG_LEVEL=N` 只压本模块（低于 3 时 `tai_log_packet` 的 JSON 格式化器连同调用一并消失）；改变日志去向（或按级别丢弃）同样是构建决策：定义 `AGENTIC_KIT_LOG` 把分发接管进你自己的宏。

媒体中间帧默认按 1/N 采样打 INFO（`AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N`，默认 50；设 0 = 关采样进 flood 模式）。flood 模式下中间帧改打 DEBUG，仅当有效上限为 4 时才输出——上限为 3 时把采样设为 0，中间帧会**完全没有日志**（比默认采样更少，别误判成丢包）。

---

## 7. 典型使用流程 {#7-典型使用流程}

```c
#include "tuya_ai.h"

// 1. 分配内存
void *mem = malloc(tai_ctx_size());

// 2. 配置
tai_config_t cfg = {
    .host             = "ai-gw.example.com",
    .port             = 8883,
    .device_id        = devid,
    .local_key        = local_key,
    .client_type      = TAI_CLIENT_DEVICE,
    .protocol_version = TAI_VER_21,
    .sign_level       = TAI_SIGN_HMAC_SHA256,
    .pal              = &my_pal,
    .on_audio         = my_on_audio,
    .on_text          = my_on_text,
    .on_image         = my_on_image,
    .on_event         = my_on_event,
    .on_disconnect    = my_on_disconnect,
};

// 3. 初始化 + 连接
tai_ctx_t *ctx = tai_ctx_init(mem, &cfg);
if (tai_connect(ctx) != TAI_OK) { /* handle error */ }

// 4. 发送文本
tai_send_text(ctx, "hello", 5);

// 5. 或发送音频流
tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000);
while (has_audio) {
    tai_send_audio_chunk(ctx, pcm_frame, frame_len);
}
tai_send_audio_end(ctx);   // 手动模式收尾；云端 VAD 模式下不要调用 audio_end

// 6. 响应通过回调异步接收...

// 7. 清理
tai_disconnect(ctx);
tai_ctx_deinit(ctx);
free(mem);
```

> 音频流的调用模式取决于 VAD 模式（云端 VAD 连续对话 vs 设备端 VAD/手动按键），详见 [VAD 与打断](../guides/vad-and-interrupt.md) 中的对比表。

---

### 应用显式获取图像密钥（仅构造） {#application-image-key}

使用公开的通用 `iot_atop_call()`，指定 API `thing.km.secret.get.device`、版本 `1.0`、`data = "{}"`。空对象依据 TuyaOpen 参考实现未传业务参数的做法，目前仅由本地 mock 测试覆盖；**实际云端是否接受尚未验证（UNVERIFIED）**。通用 API 不补充业务字段，不要自行添加 token/agent 参数。`OPRT_OK` 本身不能证明 `result.secretValue` 可用。

以下应用本地示例返回构造好的 context 或 NULL，**不调用 `tai_connect()`**，只显式请求一次 ATOP，不重试。应用提供在 `iot_init()` 之后创建的已激活 IoT client、正确对齐且至少为 `tai_ctx_size()` 字节的自有 `mem`，以及其余字段完整的 `base` 配置。`ca_cert` 参数必须提供应用信任的真实 CA（完整 PEM 或裸 Base64 正文），或传 NULL 并使用 `base->cert_bundle_attach` 中已有的平台证书包。helper 将 CA 赋给 `cfg.ca_cert`，不内置示例证书；CA 和其他借用字段须在 context 生命周期内保持有效。示例仅使用 SDK/库的公开头文件，不增加具名 SDK 封装，不改变 cJSON 的分配器钩子。

```c
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "iot_atop.h"
#include "tuya_ai.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"

/* Only for owned trees returned by cJSON_ParseWithOpts. */
static void app_wipe_json_strings(cJSON *node)
{
    for (; node; node = node->next) {
        app_wipe_json_strings(node->child);
        if (node->valuestring)
            mbedtls_platform_zeroize(node->valuestring, strlen(node->valuestring));
        if (node->string)
            mbedtls_platform_zeroize(node->string, strlen(node->string));
    }
}

static int app_decode_image_key(const char *text, uint8_t *key)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t decoded_len = 0;

    if (!text || strlen(text) != 24 || text[22] != '=' || text[23] != '=')
        return -1;
    for (size_t i = 0; i < 22; ++i) {
        const char *p = strchr(alphabet, text[i]);
        if (!p) return -1;
        /* With '==', the last sextet's four unused bits must be zero. */
        if (i == 21 && ((size_t)(p - alphabet) & 15u) != 0)
            return -1;
    }
    if (mbedtls_base64_decode(key, TAI_IMAGE_KEY_LEN, &decoded_len,
                             (const unsigned char *)text, 24) != 0)
        return -1;
    return decoded_len == TAI_IMAGE_KEY_LEN ? 0 : -1;
}

static tai_ctx_t *app_construct_with_image_key(iot_client_t *client,
                                               void *mem,
                                               const tai_config_t *base,
                                               const char *ca_cert)
{
    const iot_atop_request_t request = {
        .api = "thing.km.secret.get.device",
        .version = "1.0",
        .data = "{}"
    };
    iot_atop_response_t response = {0};
    cJSON *result = NULL;
    cJSON *secret = NULL;
    uint8_t key[TAI_IMAGE_KEY_LEN] = {0};
    tai_config_t cfg = {0};
    tai_ctx_t *ctx = NULL;

    if (!client || !mem || !base) goto cleanup;
    if (base->disable_tls ||
        ((!ca_cert || !ca_cert[0]) && !base->cert_bundle_attach)) goto cleanup;
    if (iot_atop_call(client, &request, &response) != OPRT_OK)
        goto cleanup;
    if (!response.result) goto cleanup;
    /* response.result already IS the result, not the ATOP Envelope. */
    result = cJSON_ParseWithOpts(response.result, NULL, 1);
    if (!cJSON_IsObject(result)) goto cleanup;
    secret = cJSON_GetObjectItemCaseSensitive(result, "secretValue");
    if (!cJSON_IsString(secret) || !secret->valuestring) goto cleanup;
    if (app_decode_image_key(secret->valuestring, key) != 0) goto cleanup;

    cfg = *base;
    cfg.ca_cert = ca_cert;
    cfg.image_key = key;
    ctx = tai_ctx_init(mem, &cfg);
    cfg.image_key = NULL;

cleanup:
    mbedtls_platform_zeroize(key, sizeof(key));
    app_wipe_json_strings(result);
    cJSON_Delete(result);
    if (response.result)
        mbedtls_platform_zeroize(response.result, strlen(response.result));
    mbedtls_platform_zeroize(response.error_code, sizeof(response.error_code));
    mbedtls_platform_zeroize(response.error_msg, sizeof(response.error_msg));
    iot_atop_response_free(client, &response);
    return ctx;
}
```

校验器仅接受规范的标准 Base64：长度为 24，前 22 个字符属于标准字母表，末尾恰好为 `==`，最后一个六位组的未用位为零，且解码结果恰好 16 字节。空白、URL-safe 变体、错误填充和错误长度都会在配置前被拒绝。传输/业务失败、result 缺失/null/非对象、密钥缺失/非字符串、非法 Base64 及构造失败均经同一清理路径返回 NULL。

构造成功后及所有失败路径均清零临时二进制密钥；删除解析树前，局部 helper 通过公开的 child/next 字段遍历所有节点，清零字符串值和对象成员名，包括被拒绝形状内的嵌套内容，而不只清理 secretValue。返回的明文 JSON 和 response.error_code/error_msg 也在响应释放前清零。这只是应用可见缓冲区的实际清理，不保证解析器/传输内部没有其他副本。不要记录 response、`secretValue` 或解码后的密钥。使用真实密钥前，确保 SDK 已包含该 secret API 的响应正文日志抑制；仅关闭 TAI 日志不会抑制 IoT ATOP 诊断。

成功后 context 由调用方持有：最终依次调用 `tai_disconnect(ctx)`、`tai_ctx_deinit(ctx)`，再释放 `mem`。返回 NULL 时没有构造成功的 context，`mem` 仍由调用方持有。示例不持久化固定密钥；应用若已保留经过验证的密钥，可直接传给 `tai_ctx_init()`，无需再次请求。两条路径均选择加密媒体配置；由拥有者在构造成功后调用 `tai_connect(ctx)` 并检查返回值，TLS 对端验证及会话确认成功后即可收发加密图像。helper 只检查信任来源是否配置，真正的证书验证发生在连接时；这不是已通过真实云端验证的声明。 前面的典型流程省略该字段（NULL），保持旧有行为。

---

## 8. 线程安全 {#8-线程安全}

- `tai_send_*` 函数是线程安全的，可以从任意线程调用
- 所有回调在同一个后台接收线程中按序调用
- 回调中**可以**调用 `tai_send_*()`（不持有锁）
- 回调中**不可**调用 `tai_connect`、`tai_disconnect` 或 `tai_ctx_deinit`（它们会 join worker 线程，导致自死锁）
- 如需在回调中触发断连，应调用 `tai_request_disconnect()`，再由拥有 context 的线程调用 `tai_disconnect()`
