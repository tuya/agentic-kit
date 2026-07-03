---
title: RTC TCP Client SDK 参考文档
sidebar_label: RTC TCP Client
sidebar_position: 1
---

# RTC TCP Client SDK 参考文档

## 1. 概述

**RTC TCP Client**（API 前缀 `tai_*`）是 tRTC(tuya自研RTC协议) 的 TCP 实现。以源码形式提供，通过 PAL（Platform Abstraction Layer）实现跨平台移植，适用于 POSIX、FreeRTOS（ESP-IDF）等环境。

头文件：`tuya_ai.h`（单一 include）

### 特点

- **简洁 API**：类型化发送函数（`tai_send_text`、`tai_send_audio_*`、`tai_send_image`），无需手动组装数据结构体
- **后台接收线程**：`tai_connect` 后自动启动后台线程处理接收和 keepalive
- **回调驱动**：通过 `on_audio`、`on_text`、`on_image`、`on_event`、`on_disconnect` 接收数据
- **可选自动重连**：开启 `auto_reconnect` 后，`tai_send_*` 在链路断开时会自动重连再发送（详见 3.6 / 9），默认关闭以保
- **无外部依赖**：用户 PAL 只需提供原始 TCP 和平台原语；TLS 和加密由 SDK 内部通过 bundled mbedTLS 处理


---

## 2. 常量定义

### 2.1 数据包类型

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_PKT_CLIENT_HELLO` | 1 | 客户端握手 |
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

### 2.2 流标志（Stream Flags）

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_STREAM_ONE_SHOT` | 0x00 | 单包（完整数据） |
| `TAI_STREAM_START` | 0x01 | 流开始 |
| `TAI_STREAM_MIDDLE` | 0x02 | 流中间 |
| `TAI_STREAM_END` | 0x03 | 流结束 |

### 2.3 事件类型

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_EVT_START` | 0 | 会话开始 |
| `TAI_EVT_PAYLOADS_END` | 1 | 负载结束 |
| `TAI_EVT_END` | 2 | 会话结束 |
| `TAI_EVT_ONE_SHOT` | 3 | 单次事件 |
| `TAI_EVT_CHAT_BREAK` | 4 | 聊天打断（用户中途说话） |
| `TAI_EVT_SERVER_VAD` | 5 | 云端 VAD 检测到用户停止说话 |
| `TAI_EVT_MCP_CMD` | 1000 | MCP 命令（设备侧执行） |
| `TAI_EVT_SERVER_TIMEOVER` | 1001 | 服务端超时 |
| `TAI_EVT_UPDATE_CONTEXT` | 1002 | 上下文更新 |

### 2.4 客户端类型

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_CLIENT_DEVICE` | 1 | 设备端 |
| `TAI_CLIENT_APP` | 2 | 应用端 |

### 2.5 音频编码

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_AUDIO_PCM` | 101 | 原始 PCM |
| `TAI_AUDIO_OPUS` | 111 | Opus 编码 |

### 2.6 图像格式

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_IMG_JPEG` | 1 | JPEG |
| `TAI_IMG_PNG` | 2 | PNG |

### 2.7 图像负载类型

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_IMG_PAYLOAD_RAW` | 0 | 原始二进制 |
| `TAI_IMG_PAYLOAD_BASE64` | 1 | Base64 编码 |
| `TAI_IMG_PAYLOAD_URL` | 2 | URL 字符串 |

### 2.8 签名级别

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_SIGN_NONE` | 0 | 不签名 |
| `TAI_SIGN_HMAC_SHA1` | 1 | HMAC-SHA1 |
| `TAI_SIGN_HMAC_SHA256` | 2 | HMAC-SHA256（推荐） |

### 2.9 数据 ID

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_DATA_ID_AUDIO_UP` | 1 | 上行音频 |
| `TAI_DATA_ID_AUDIO_DOWN` | 2 | 下行音频 |
| `TAI_DATA_ID_TEXT_UP` | 3 | 上行文本 |
| `TAI_DATA_ID_TEXT_DOWN` | 4 | 下行文本 |
| `TAI_DATA_ID_IMAGE_UP` | 5 | 上行图像 |
| `TAI_DATA_ID_AUDIO_AUX` | 7 | 辅助音频 |

### 2.10 返回码

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

## 3. 配置（`tai_config_t`）

调用 `tai_ctx_init` 前填充此结构体。所有指针字段在 context 生命周期内必须保持有效。

### 3.1 服务器配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `host` | `const char *` | 服务器地址（通常从 `iot_client_get_session_token()` 返回的 token 中解析得到） |
| `port` | `uint16_t` | 服务器端口 |
| `tls_sni` | `const char *` | TLS SNI 主机名（通常与 host 相同；若 token 中提供域名，优先使用域名） |

### 3.2 身份配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `device_id` | `const char *` | 设备 ID（配网后获得的 devid） |
| `local_key` | `const char *` | 本地密钥（配网后获得的 local_key） |
| `client_type` | `uint8_t` | 客户端类型：`TAI_CLIENT_DEVICE` 或 `TAI_CLIENT_APP` |
| `protocol_version` | `uint8_t` | 协议版本：使用 `TAI_VER_21` |

### 3.3 会话选项

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_attrs_json` | `const char *` | 会话属性 JSON（NULL 使用默认值）。可配置云端 VAD、音频格式等 |
| `event_user_data_json` | `const char *` | 事件用户数据 JSON（NULL 使用默认值） |
| `agent_token` | `const char *` | Agent Token（指定特定 Agent，NULL 使用产品默认 Agent） |

### 3.4 业务标识

| 字段 | 类型 | 说明 |
|------|------|------|
| `biz_code` | `uint32_t` | 业务码 |
| `biz_tag` | `uint64_t` | 业务标签 |

### 3.5 安全配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `sign_level` | `uint8_t` | 签名级别：`TAI_SIGN_NONE` / `TAI_SIGN_HMAC_SHA256`（推荐） |

### 3.6 连接与保活配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `ping_interval_ms` | `uint32_t` | Ping 间隔（0 = 默认 60000ms） |
| `ping_timeout_ms` | `uint32_t` | Ping 超时（0 = 默认 90000ms） |
| `connect_timeout_ms` | `uint32_t` | 连接超时（0 = 默认 5000ms）。分别约束 `tai_connect` 的两个串行等待阶段：先是连接建立（TCP 连接 + TLS 握手，共用一个预算），再是服务端 SessionNew 应答。任一阶段超时即判定连接失败，因此 `tai_connect` 最坏耗时约为该值的 2 倍（建连 + 应答）。同一预算也约束 `auto_reconnect` 触发的重连。 |
| `auto_reconnect` | `uint8_t` | 自动重连（connect-on-send），0 = 关闭（默认）。非零时，链路已断的情况下调用 `tai_send_*` 会先自动（重新）建连再发送，重连受 `connect_timeout_ms` 限时；限时内连不上则该次发送返回错误。详见第 9 节。 |

### 3.7 测试配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `disable_tls` | `uint8_t` | 非零时跳过 TLS，仅用于集成测试 |

### 3.8 平台适配

| 字段 | 类型 | 说明 |
|------|------|------|
| `pal` | `const pal_t *` | 平台适配层实现指针 |

### 3.9 回调函数

所有回调均在后台接收线程中调用。

| 字段 | 类型 | 说明 |
|------|------|------|
| `on_audio` | function pointer | 音频数据回调 |
| `on_text` | function pointer | 文本数据回调 |
| `on_image` | function pointer | 图像数据回调（云端生成的图片） |
| `on_event` | function pointer | 事件回调（MCP、打断、VAD 等） |
| `on_disconnect` | function pointer | 断连回调 |
| `user_data` | `void *` | 透传到所有回调 |

**回调签名：**

每个回调接收**单个** `const` 消息结构体指针 + `user_data`（不再是旧的多参数形式）。

```c
void (*on_audio)     (tai_ctx_t *ctx, const tai_audio_msg_t      *msg, void *user_data);
void (*on_text)      (tai_ctx_t *ctx, const tai_text_msg_t       *msg, void *user_data);
void (*on_image)     (tai_ctx_t *ctx, const tai_image_msg_t      *msg, void *user_data);
void (*on_event)     (tai_ctx_t *ctx, const tai_event_msg_t      *msg, void *user_data);
void (*on_disconnect)(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user_data);
```

### 接收消息结构体

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
| `timestamp_ms` | `uint64_t` | 流起始时间戳（媒体头） |

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
| `data` | `const uint8_t *` | 编码图像字节（JPEG/PNG）；回调生命周期内有效 |
| `len` | `size_t` | 数据字节数 |
| `format` | `uint8_t` | `TAI_IMG_JPEG` / `TAI_IMG_PNG` / 0=未知 |
| `width` | `uint16_t` | 宽（px），未知或非 START/ONE_SHOT 包时为 0 |
| `height` | `uint16_t` | 高（px），未知或非 START/ONE_SHOT 包时为 0 |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | 数据 ID（取自媒体头） |
| `event_id` | `const char *` | turn id（借用）；无则为 `""` |
| `timestamp_ms` | `uint64_t` | 流起始时间戳（媒体头） |

:::note 流式接收
接收到的图片以分片流形式到达：START（或 ONE_SHOT）携带首字节与 image-params，MIDDLE 继续，END 结束（len 可能为 0）。调用方按 `stream_flag` 累积分片，在流结束后（END 或 ONE_SHOT）解码整图。`format`/`width`/`height` 仅在 START/ONE_SHOT 从 image-params 解析得到，MIDDLE/END 时为 0。
:::

`tai_event_msg_t`（事件回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `event_type` | `uint16_t` | `TAI_EVT_*` |
| `data` | `const uint8_t *` | 事件负载（通常为 JSON） |
| `len` | `size_t` | 负载字节数 |
| `event_id` | `const char *` | attr 61（借用）；无则为 `""` |

`tai_disconnect_msg_t`（断连回调）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `reason` | `uint8_t` | `TAI_DISCONNECT_*`（见下表） |
| `close_code` | `uint16_t` | 服务端关闭码（SESSION/CONNECTION）；其他情况为 0 |
| `detail` | `uint8_t` | `TAI_TRANSPORT_*` / `TAI_PROTO_ERR_*`（见下表）；其他情况为 0 |
| `connection_alive` | `uint8_t` | 仅当 `reason==SESSION_CLOSE` 时为 1 |
| `session_id` | `char[64]` | 值拷贝；无则为 `""` |

:::warning 生命周期
`msg` 以及其内部所有指针（`data` / `text` / `event_id` 等）**仅在回调执行期间有效**——SDK 从不堆分配它们。如需在回调返回后保留，请自行拷贝。
:::

#### 断连原因（`reason`）

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_DISCONNECT_SESSION_CLOSE` | 0 | 服务端 SessionClose；链路可能仍存活（`connection_alive==1`） |
| `TAI_DISCONNECT_CONNECTION_CLOSE` | 1 | 服务端 ConnectionClose；worker 停止 |
| `TAI_DISCONNECT_TRANSPORT` | 2 | worker 检测到传输层故障 |
| `TAI_DISCONNECT_PROTOCOL` | 3 | fail-fast：解析/行为错误 |

#### `detail`（当 `reason==TRANSPORT`）

| 宏 | 值 | 说明 |
|----|---|------|
| `TAI_TRANSPORT_PING_TIMEOUT` | 1 | Ping 超时 |
| `TAI_TRANSPORT_EOF` | 2 | 对端关闭（EOF） |
| `TAI_TRANSPORT_NET_ERROR` | 3 | 网络错误 |

#### `detail`（当 `reason==PROTOCOL`）

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

---

## 4. 生命周期 API

### `tai_ctx_size`

```c
size_t tai_ctx_size(void);
```

返回 context 所需的内存大小。调用方需分配至少此大小的内存，传给 `tai_ctx_init`。

---

### `tai_ctx_init`

```c
tai_ctx_t *tai_ctx_init(void *mem, const tai_config_t *cfg);
```

初始化 context。`mem` 必须至少为 `tai_ctx_size()` 字节，且在 context 生命周期内保持有效。

**参数：**
- `mem` — 预分配的内存块
- `cfg` — 配置结构体

**返回值：** 成功返回 `tai_ctx_t*`（即 `mem` 强转），失败返回 NULL。

---

### `tai_ctx_deinit`

```c
void tai_ctx_deinit(tai_ctx_t *ctx);
```

反初始化 context，释放内部资源。调用后 `mem` 可以 free。必须在 `tai_disconnect` 之后调用。

---

### `tai_connect`

```c
int tai_connect(tai_ctx_t *ctx);
```

执行 TLS 握手、发送 ClientHello、建立会话、启动后台接收线程。

**返回值：** `TAI_OK` 成功，`TAI_ERR_*` 失败。

**行为：**
- 阻塞直到连接建立或失败
- 成功后，后台线程开始处理 Ping/Pong 和接收数据
- 连接成功后即可调用 `tai_send_*` 系列函数
- 若开启了 `auto_reconnect`，可不显式调用本函数——首个 `tai_send_*` 会在未连接时自动建连（但仍建议显式调用以便处理首次连接错误）

---

### `tai_disconnect`

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

### `tai_request_disconnect`

```c
void tai_request_disconnect(tai_ctx_t *ctx);
```

请求后台 worker 停止，但**不** join、**不**拆除连接。可从**任意**线程调用，包括从接收回调内部调用（与 `tai_disconnect` 不同）。worker 会在下一轮循环退出。

**用途：** 在回调中安全地触发断连。调用后，拥有 context 的线程仍需调用 `tai_disconnect()` 来 join worker、发送 SessionClose 并释放资源。

---

## 5. 发送 API

> 默认所有发送在链路未建立时都返回错误（快速失败）。若开启 `auto_reconnect`，**起始类**发送会先自动重连再发送，而续流类 `tai_send_audio_chunk` / `tai_send_audio_end` 仍快速失败——完整分类表与错误处理见第 9 节（9.1 / 9.2）。

### `tai_send_text`

```c
int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len);
```

发送文本消息。单包完成（内部设置 `ONE_SHOT` 标志）。

**参数：**
- `text` — UTF-8 文本
- `len` — 文本字节数

**返回值：** `TAI_OK` 或 `TAI_ERR_*`。

---

### `tai_send_audio_start`

```c
int tai_send_audio_start(tai_ctx_t *ctx,
                         uint8_t codec, uint8_t channels,
                         uint8_t bit_depth, uint32_t sample_rate);
```

开始一段音频流。必须在 `tai_send_audio_chunk` 之前调用。

**参数：**
- `codec` — 编码格式：`TAI_AUDIO_PCM`（101）或 `TAI_AUDIO_OPUS`（111）
- `channels` — 声道数（通常为 1）
- `bit_depth` — 位深（通常为 16）
- `sample_rate` — 采样率（通常为 16000）

---

### `tai_send_audio_chunk`

```c
int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len);
```

发送一帧音频数据。可多次调用（流中间包）。

**参数：**
- `pcm` — 音频数据（PCM 或 Opus 帧，取决于 `audio_start` 时的 codec）
- `len` — 数据字节数

---

### `tai_send_audio_end`

```c
int tai_send_audio_end(tai_ctx_t *ctx);
```

结束音频流。通知服务端本次音频输入完毕，开始处理。

---

### `tai_send_image`

```c
int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height);
```

发送单张图片。

**参数：**
- `data` — 图片二进制数据
- `len` — 数据长度
- `format` — 图片格式：`TAI_IMG_JPEG`（1）或 `TAI_IMG_PNG`（2）
- `width` / `height` — 图片尺寸

---

### `tai_send_image_with_text`

```c
int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height);
```

同时发送文本和图片（如图片理解场景）。内部会先发文本包再发图片包，组成一次完整请求。

---

### `tai_chat_break`

```c
int tai_chat_break(tai_ctx_t *ctx);
```

发送聊天打断事件。用于用户主动打断 AI 回复（如按键中断）。服务端收到后会停止当前响应生成。

---

### `tai_send_mcp_response`

```c
int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response);
```

发送 MCP 命令的响应。当 `on_event` 收到 `TAI_EVT_MCP_CMD` 时，设备执行完命令后通过此函数返回结果。

**参数：**
- `json_rpc_response` — JSON-RPC 格式的响应字符串

---

## 6. 日志

```c
static inline void tai_set_log_level(int level);
static inline int  tai_get_log_level(void);
```

设置/获取运行时日志级别。有效值：

| 值 | 含义 |
|----|------|
| 0 | 禁用所有日志 |
| 1 | `TAI_LOG_ERROR` |
| 2 | `TAI_LOG_WARN` |
| 3 | `TAI_LOG_INFO` |
| 4 | `TAI_LOG_DEBUG`（默认） |

编译时可通过定义 `TAI_LOG_LEVEL` 宏设置最大编译级别（超过的日志在编译期消除）。

---

## 7. 典型使用流程

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
tai_send_audio_end(ctx);

// 6. 响应通过回调异步接收...

// 7. 清理
tai_disconnect(ctx);
tai_ctx_deinit(ctx);
free(mem);
```

---

## 8. 线程安全

- `tai_send_*` 函数是线程安全的，可以从任意线程调用
- 所有回调在同一个后台接收线程中按序调用
- 回调中**可以**调用 `tai_send_*()`（不持有锁）
- 回调中**不可**调用 `tai_connect`、`tai_disconnect` 或 `tai_ctx_deinit`（它们会 join worker 线程，导致自死锁）
- 如需在回调中触发断连，应调用 `tai_request_disconnect()`，再由拥有 context 的线程调用 `tai_disconnect()`
- 开启 `auto_reconnect` 后，链路断开时的 `tai_send_*` 会在内部执行重连（join + connect），因此这类发送必须在应用线程调用，**不可**在接收回调中调用（同样会自死锁）；回调中的发送只应针对当前存活的连接

---

## 9. 自动重连（connect-on-send，可选）

默认情况下 SDK 是 **fail-fast**：链路断开后 worker 触发一次 `on_disconnect` 并退出，由应用自行 `tai_disconnect()` + `tai_connect()` 重连（示例见 `examples/posix/ai/rtc-tcp-client/demo_reconnect.h`：指数退避 + 熔断）。

开启 `cfg.auto_reconnect = 1` 后，改为 **发送时按需重连（connect-on-send）**：调用发送时若链路未建立，先在内部（重新）建连——等价于 `tai_disconnect()`（join 已退出的 worker、关闭旧 socket、复位）+ `tai_connect()`（重建 + 新 worker），受 `connect_timeout_ms` 限时——成功后再发送。

### 9.1 哪些 API 会自动重连，哪些只会快速失败

**关键区分**：只有**起始类**发送（开启一个新会话/新流/一次性消息）会按需重连；**续流类**发送（依赖一个已经打开的音频 event）永远**快速失败**，因为重连得到的是一个全新会话，没有那个已打开的 event。

| API | `auto_reconnect=1` 且链路已断 | `auto_reconnect=0`（默认）且链路已断 |
|------|------|------|
| `tai_send_text` | ✅ 自动重连后发送 | ❌ 快速失败 |
| `tai_send_audio_start` | ✅ 自动重连（起一个新流） | ❌ 快速失败 |
| `tai_send_image` | ✅ 自动重连后发送 | ❌ 快速失败 |
| `tai_send_image_with_text` | ✅ 自动重连后发送 | ❌ 快速失败 |
| `tai_chat_break` | ✅ 自动重连后发送 | ❌ 快速失败 |
| `tai_send_mcp_response` | ✅ 自动重连后发送 | ❌ 快速失败 |
| `tai_send_audio_chunk` | ❌ **快速失败**（续流,不重连） | ❌ 快速失败 |
| `tai_send_audio_end` | ❌ **快速失败**（续流,不重连） | ❌ 快速失败 |

> "快速失败" = 立即返回错误码、不建连、不发送、不阻塞。

其它语义：

- **限时失败即信号**：自动重连若在 `connect_timeout_ms` 内连不上，该次发送返回错误码，应用据此感知；**不会**为 connect-on-send 失败触发 `on_disconnect`，因此 `on_disconnect` 始终只在 worker 线程回调。
- **静默愈合**：能被下次起始发送愈合的断线不打扰应用——worker 的终止性 `on_disconnect` 被抑制；只有服务端 `SESSION_CLOSE`（`connection_alive=1`）这类**非终止**事件仍正常回调。
- **并发安全 + 线程约束**：并发重连由内部专用锁串行化；但由于重连会 join worker 线程，会触发重连的发送必须在应用线程调用，不可在接收回调中调用（见第 8 节）。

### 9.2 遇到快速失败（发送返回非 `TAI_OK`）怎么处理

发送返回错误就是"这条没发出去"的信号，按场景处理：

1. **续流发送（`tai_send_audio_chunk` / `tai_send_audio_end`）返回错误**：说明音频流所在的连接已断，这个流**无法续传**。正确做法是**放弃本流剩余分片，重新 `tai_send_audio_start` 起一个新流**（该调用会触发自动重连），再从头开始 `tai_send_audio_chunk`。不要重试同一个 chunk。
2. **起始类发送返回错误（且 `auto_reconnect=1`）**：说明在 `connect_timeout_ms` 内没能建连。做法是**退避后重试该发送**（下一次调用会再尝试重连），或据业务判定暂时不可用；不要在紧循环里立即重试（会连击服务器）。
3. **任何发送返回错误（且 `auto_reconnect=0`，默认）**：这是传统 fail-fast 语义——由**应用侧**驱动重连：`tai_disconnect()` + `tai_connect()`，配合退避 + 熔断，然后重发。可直接照抄 `examples/posix/ai/rtc-tcp-client/demo_reconnect.h`。

一段音频续流的健壮写法（开启自动重连）：

```c
if (tai_send_audio_chunk(ctx, pcm, n) != TAI_OK) {
    /* 续流不重连：本流已断，丢弃剩余分片，重起一个新流（会自动重连）。*/
    if (tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000) != TAI_OK) {
        /* 连都没连上：退避后稍后再试 */
    } else {
        tai_send_audio_chunk(ctx, pcm, n);   /* 在新流上重发本帧 */
    }
}
```

开启后甚至可以省略显式的 `tai_connect`——首个发送会自动建连：

```c
tai_config_t cfg = {
    /* ... host / device_id / local_key / pal / 回调 ... */
    .connect_timeout_ms = 5000,   // 每次（重）连的时间预算
    .auto_reconnect     = 1,      // 开启发送时按需重连
};
tai_ctx_t *ctx = tai_ctx_init(mem, &cfg);

/* 无需先调用 tai_connect：首个发送会自动建连；
 * 之后若链路断开，下一次发送会静默重连。 */
if (tai_send_text(ctx, "hello", 5) != TAI_OK) {
    /* connect_timeout_ms 内未能建连：本次发送失败，可稍后重试 */
}

/* 结束时仍由应用线程收尾 */
tai_disconnect(ctx);
tai_ctx_deinit(ctx);
```

> 何时用哪种：需要精细控制退避/熔断、或从多线程并发发送时，仍推荐关闭 `auto_reconnect` 并采用应用侧 `demo_reconnect.h` 模式；只想“发送即连、断了自动重连”的简单场景，开启 `auto_reconnect` 即可。
