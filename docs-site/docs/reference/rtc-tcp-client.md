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
- **回调驱动**：通过 `on_audio`、`on_text`、`on_event`、`on_disconnect` 接收数据
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

### 3.6 保活配置

| 字段 | 类型 | 说明 |
|------|------|------|
| `ping_interval_ms` | `uint32_t` | Ping 间隔（0 = 默认 60000ms） |
| `ping_timeout_ms` | `uint32_t` | Ping 超时（0 = 默认 90000ms） |

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
| `on_event` | function pointer | 事件回调（MCP、打断、VAD 等） |
| `on_disconnect` | function pointer | 断连回调 |
| `user_data` | `void *` | 透传到所有回调 |

**回调签名：**

```c
void (*on_audio)(tai_ctx_t *ctx,
                 const uint8_t *data, size_t len,
                 uint32_t sample_rate, uint16_t frame_duration,
                 void *user_data);

void (*on_text)(tai_ctx_t *ctx,
                const char *text, size_t len,
                uint8_t stream_flag,    // TAI_STREAM_*
                void *user_data);

void (*on_event)(tai_ctx_t *ctx,
                 uint16_t event_type,   // TAI_EVT_*
                 const uint8_t *data, size_t len,
                 void *user_data);

void (*on_disconnect)(tai_ctx_t *ctx,
                      uint16_t error_code,
                      void *user_data);
```

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

---

### `tai_disconnect`

```c
void tai_disconnect(tai_ctx_t *ctx);
```

停止后台接收线程，发送 SessionClose + ConnectionClose，关闭 TLS 连接。

**行为：**
- 阻塞直到后台线程退出
- 调用后不可再发送数据
- 之后应调用 `tai_ctx_deinit`

---

## 5. 发送 API

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
- 不要在回调中调用 `tai_disconnect` 或 `tai_ctx_deinit`（会死锁）
- 如需在回调中触发断连，应通过设置标志位由其他线程执行
