---
title: RTC TCP Client SDK Reference
sidebar_label: RTC TCP Client
sidebar_position: 1
---

# RTC TCP Client SDK Reference

## 1. Overview {#1-概述}

**RTC TCP Client** (API prefix `tai_*`) is the TCP implementation of tRTC (Tuya's proprietary RTC protocol). It is provided as source code and supports cross-platform porting through the PAL (Platform Abstraction Layer), making it suitable for environments such as POSIX and FreeRTOS (ESP-IDF).

Header file: `tuya_ai.h` (single include)

### Features {#特点}

- **Simple API**: Typed send functions (`tai_send_text`, `tai_send_audio_*`, and `tai_send_image`) eliminate the need to manually assemble data structures
- **Background receive thread**: Automatically starts a background thread after `tai_connect` to handle receiving and Keepalive
- **Callback-driven**: Receives data through `on_audio`, `on_text`, `on_image`, `on_event`, and `on_disconnect`
- **No external dependencies**: The user-provided PAL only needs to supply raw TCP and platform primitives; the SDK handles TLS and cryptography internally through bundled mbedTLS


---

## 2. Constant Definitions {#2-常量定义}

### 2.1 Packet Types {#21-数据包类型}

| Macro | Value | Description |
|----|---|------|
| `TAI_PKT_CLIENT_HELLO` | 1 | Client handshake |
| `TAI_PKT_AUTHENTICATE_RESPONSE` | 3 | Server authentication result for ClientHello |
| `TAI_PKT_PING` | 4 | Keepalive Ping request |
| `TAI_PKT_PONG` | 5 | Keepalive Pong response |
| `TAI_PKT_CONNECTION_CLOSE` | 6 | Close Connection |
| `TAI_PKT_SESSION_NEW` | 7 | Create Session |
| `TAI_PKT_SESSION_CLOSE` | 8 | Close Session |
| `TAI_PKT_CONNECTION_REFRESH_REQ` | 9 | Connection refresh request |
| `TAI_PKT_CONNECTION_REFRESH_RESP` | 10 | Connection refresh response |
| `TAI_PKT_VIDEO` | 30 | Video data |
| `TAI_PKT_AUDIO` | 31 | Audio data |
| `TAI_PKT_IMAGE` | 32 | Image data |
| `TAI_PKT_FILE` | 33 | File data |
| `TAI_PKT_TEXT` | 34 | Text data |
| `TAI_PKT_EVENT` | 35 | Event data |

### 2.2 Stream Flags {#22-流标志stream-flags}

| Macro | Value | Description |
|----|---|------|
| `TAI_STREAM_ONE_SHOT` | 0x00 | Single Packet (complete data) |
| `TAI_STREAM_START` | 0x01 | Stream start |
| `TAI_STREAM_MIDDLE` | 0x02 | Stream middle |
| `TAI_STREAM_END` | 0x03 | Stream end |

### 2.3 Event Types {#23-事件类型}

| Macro | Value | Description |
|----|---|------|
| `TAI_EVT_START` | 0 | Session start |
| `TAI_EVT_PAYLOADS_END` | 1 | End of payloads |
| `TAI_EVT_END` | 2 | Session end |
| `TAI_EVT_ONE_SHOT` | 3 | One-shot Event |
| `TAI_EVT_CHAT_BREAK` | 4 | Chat break/cloud VAD turn end (the user stops speaking or interrupts mid-response); the current cloud uses this as the turn-boundary signal; only clear the downlink TTS buffer/playback queue and do not end the uplink Event |
| `TAI_EVT_SERVER_VAD` | 5 | Server VAD detects that the user has stopped speaking (legacy server signal; **the current cloud no longer sends it**, and the constant is retained only for protocol compatibility) |
| `TAI_EVT_MCP_CMD` | 1000 | MCP command (executed on the device) |
| `TAI_EVT_SERVER_TIMEOVER` | 1001 | Server timeout |
| `TAI_EVT_UPDATE_CONTEXT` | 1002 | Context update |

### 2.4 Client Types {#24-客户端类型}

| Macro | Value | Description |
|----|---|------|
| `TAI_CLIENT_DEVICE` | 1 | Device |
| `TAI_CLIENT_APP` | 2 | App |

### 2.5 Audio Codecs {#25-音频编码}

| Macro | Value | Description |
|----|---|------|
| `TAI_AUDIO_PCM` | 101 | Raw PCM |
| `TAI_AUDIO_OPUS` | 111 | Opus encoding |

### 2.6 Image Formats {#26-图像格式}

| Macro | Value | Description |
|----|---|------|
| `TAI_IMG_JPEG` | 1 | JPEG |
| `TAI_IMG_PNG` | 2 | PNG |

### 2.7 Image Payload Types {#27-图像负载类型}

| Macro | Value | Description |
|----|---|------|
| `TAI_IMG_PAYLOAD_RAW` | 0 | Raw binary |
| `TAI_IMG_PAYLOAD_BASE64` | 1 | Base64 encoding |
| `TAI_IMG_PAYLOAD_URL` | 2 | URL string |

### 2.8 Sign Levels {#28-签名级别}

| Macro | Value | Description |
|----|---|------|
| `TAI_SIGN_NONE` | 0 | No signature |
| `TAI_SIGN_HMAC_SHA1` | 1 | HMAC-SHA1 |
| `TAI_SIGN_HMAC_SHA256` | 2 | HMAC-SHA256 (recommended) |

### 2.9 Data IDs {#29-数据-id}

| Macro | Value | Description |
|----|---|------|
| `TAI_DATA_ID_AUDIO_UP` | 1 | Uplink audio |
| `TAI_DATA_ID_AUDIO_DOWN` | 2 | Downlink audio |
| `TAI_DATA_ID_TEXT_UP` | 3 | Uplink text |
| `TAI_DATA_ID_TEXT_DOWN` | 4 | Downlink text |
| `TAI_DATA_ID_IMAGE_UP` | 5 | Uplink image |
| `TAI_DATA_ID_AUDIO_AUX` | 7 | Auxiliary audio |

### 2.10 Return Codes {#210-返回码}

| Macro | Value | Description |
|----|---|------|
| `TAI_OK` | 0 | Success |
| `TAI_ERR_ARGS` | -1 | Invalid argument |
| `TAI_ERR_MEM` | -2 | Insufficient memory |
| `TAI_ERR_NET` | -3 | Network error |
| `TAI_ERR_TLS` | -4 | TLS error |
| `TAI_ERR_PROTO` | -5 | Protocol error |
| `TAI_ERR_HMAC` | -6 | HMAC verification failed |
| `TAI_ERR_AGAIN` | -7 | Retry required |
| `TAI_ERR_CRYPTO` | -8 | Encryption/decryption error |

---

## 3. Configuration (`tai_config_t`) {#3-配置tai_config_t}

Populate this structure before calling `tai_ctx_init`. All pointer fields must remain valid for the lifetime of the context.

### 3.1 Server Configuration {#31-服务器配置}

| Field | Type | Description |
|------|------|------|
| `host` | `const char *` | Server address (usually parsed from the token returned by `iot_client_get_session_token()`) |
| `port` | `uint16_t` | Server port |
| `tls_sni` | `const char *` | TLS SNI hostname (usually the same as host; if the token provides a domain name, prefer the domain name) |

### 3.2 Identity Configuration {#32-身份配置}

| Field | Type | Description |
|------|------|------|
| `device_id` | `const char *` | Device ID (devid obtained after Provisioning) |
| `local_key` | `const char *` | Local key (local_key obtained after Provisioning) |
| `client_type` | `uint8_t` | Client type: `TAI_CLIENT_DEVICE` or `TAI_CLIENT_APP` (0 = default `TAI_CLIENT_DEVICE`) |
| `protocol_version` | `uint8_t` | Protocol version: use `TAI_VER_21` (0 = default `TAI_VER_21`) |

### 3.3 Session Options {#33-会话选项}

| Field | Type | Description |
|------|------|------|
| `session_attrs_json` | `const char *` | Session attributes JSON (NULL uses the default). Can configure Server VAD, audio format, and other options |
| `event_user_data_json` | `const char *` | Event user data JSON (NULL uses the default) |
| `agent_token` | `const char *` | Agent Token (specifies a particular Agent; NULL uses the product's default Agent) |

### 3.4 Business Identifiers {#34-业务标识}

| Field | Type | Description |
|------|------|------|
| `biz_code` | `uint32_t` | Business code (0 = default `65537`) |
| `biz_tag` | `uint64_t` | Business tag (0 = default `119`) |

### 3.5 Security Configuration {#35-安全配置}

| Field | Type | Description |
|------|------|------|
| `sign_level` | `uint8_t` | Sign level: `TAI_SIGN_HMAC_SHA256` (recommended) or `TAI_SIGN_HMAC_SHA1`. Note that `TAI_SIGN_NONE` (0) is treated as "unset" and forcibly falls back to `TAI_SIGN_HMAC_SHA256`; signing cannot be disabled through this field |

### 3.6 Keepalive Configuration {#36-保活配置}

| Field | Type | Description |
|------|------|------|
| `ping_interval_ms` | `uint32_t` | Ping interval (0 = default 60000ms) |
| `ping_timeout_ms` | `uint32_t` | Ping timeout (0 = default 90000ms) |
| `connect_timeout_ms` | `uint32_t` | Connection timeout (0 = default 5000ms). It separately constrains the two sequential wait stages of `tai_connect`: first Connection establishment (TCP connection + TLS handshake, sharing one budget), then the server's SessionNew response. A timeout in either stage means the Connection failed, so the worst-case duration of `tai_connect` is approximately twice this value. |

### 3.7 Test Configuration {#37-测试配置}

| Field | Type | Description |
|------|------|------|
| `disable_tls` | `uint8_t` | If nonzero, skips TLS; for integration testing only |

### 3.8 Platform Adaptation {#38-平台适配}

| Field | Type | Description |
|------|------|------|
| `pal` | `const pal_t *` | Pointer to the platform adaptation layer implementation |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | Platform certificate bundle callback (set to `esp_crt_bundle_attach` on ESP-IDF; otherwise TLS does not verify certificates); NULL means unused. See [TLS Certificate Verification](../guides/tls-cert-verification.md) |

### 3.9 Callbacks {#39-回调函数}

All callbacks are invoked on the background receive thread.

| Field | Type | Description |
|------|------|------|
| `on_audio` | function pointer | Audio data callback |
| `on_text` | function pointer | Text data callback |
| `on_image` | function pointer | Image data callback (images generated by the cloud) |
| `on_event` | function pointer | Event callback (MCP commands, Chat break, VAD, and so on) |
| `on_disconnect` | function pointer | Disconnect callback |
| `user_data` | `void *` | Passed through to all callbacks |

**Callback signatures:**

Each callback receives a **single** pointer to a `const` message structure plus `user_data` (instead of the former multi-argument form).

```c
void (*on_audio)     (tai_ctx_t *ctx, const tai_audio_msg_t      *msg, void *user_data);
void (*on_text)      (tai_ctx_t *ctx, const tai_text_msg_t       *msg, void *user_data);
void (*on_image)     (tai_ctx_t *ctx, const tai_image_msg_t      *msg, void *user_data);
void (*on_event)     (tai_ctx_t *ctx, const tai_event_msg_t      *msg, void *user_data);
void (*on_disconnect)(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user_data);
```

### Received Message Structures {#接收消息结构体}

`tai_audio_msg_t` (audio callback):

| Field | Type | Description |
|------|------|------|
| `data` | `const uint8_t *` | Opus frame / PCM bytes |
| `len` | `size_t` | Data length in bytes |
| `codec` | `uint8_t` | `TAI_AUDIO_OPUS` / `TAI_AUDIO_PCM` / 0=unknown |
| `sample_rate` | `uint32_t` | Sample rate (Hz), 0=unknown |
| `frame_duration` | `uint16_t` | Duration of each Opus frame (ms) |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` (from the media header) |
| `data_id` | `uint16_t` | Data ID: `AUDIO_DOWN`(2) / `AUDIO_AUX`(7) |
| `event_id` | `const char *` | Turn ID (borrowed); `""` if absent |
| `timestamp_ms` | `uint64_t` | Stream start timestamp (media header) |

`tai_text_msg_t` (text callback):

| Field | Type | Description |
|------|------|------|
| `text` | `const char *` | UTF-8 text, **not** NUL-terminated |
| `len` | `size_t` | Text length in bytes |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | Data ID: `TAI_DATA_ID_TEXT_DOWN`(4) |
| `seq` | `uint32_t` | Text sequence number within each Event (varint) |
| `event_id` | `const char *` | Turn ID (borrowed); `""` if absent |

`tai_image_msg_t` (image callback):

| Field | Type | Description |
|------|------|------|
| `data` | `const uint8_t *` | Encoded image bytes (JPEG/PNG); valid for the callback lifetime |
| `len` | `size_t` | Data length in bytes |
| `format` | `uint8_t` | `TAI_IMG_JPEG` / `TAI_IMG_PNG` / 0=unknown |
| `width` | `uint16_t` | Width (px), 0 if unknown or if this is not a START/ONE_SHOT Packet |
| `height` | `uint16_t` | Height (px), 0 if unknown or if this is not a START/ONE_SHOT Packet |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | Data ID (from the media header) |
| `event_id` | `const char *` | Turn ID (borrowed); `""` if absent |
| `timestamp_ms` | `uint64_t` | Stream start timestamp (media header) |

:::note Streaming reception
Received images arrive as a stream of chunks: START (or ONE_SHOT) carries the first bytes and image-params, MIDDLE continues the stream, and END finishes it (len may be 0). The caller accumulates the chunks according to the `stream_flag` and decodes the complete image after the stream ends (END or ONE_SHOT). `format`/`width`/`height` are parsed from image-params only for START/ONE_SHOT and are 0 for MIDDLE/END.
:::

`tai_event_msg_t` (Event callback):

| Field | Type | Description |
|------|------|------|
| `event_type` | `uint16_t` | `TAI_EVT_*` |
| `data` | `const uint8_t *` | Event payload (usually JSON) |
| `len` | `size_t` | Payload length in bytes |
| `event_id` | `const char *` | attr 61 (borrowed); `""` if absent |

`tai_disconnect_msg_t` (disconnect callback):

| Field | Type | Description |
|------|------|------|
| `reason` | `uint8_t` | `TAI_DISCONNECT_*` (see the table that follows) |
| `close_code` | `uint16_t` | Server close code (SESSION/CONNECTION); 0 in other cases |
| `detail` | `uint8_t` | `TAI_TRANSPORT_*` / `TAI_PROTO_ERR_*` (see the tables that follow); 0 in other cases |
| `connection_alive` | `uint8_t` | 1 only when `reason==SESSION_CLOSE` |
| `session_id` | `char[64]` | Copied value; `""` if absent |

:::warning Lifetime
`msg` and all pointers within it (`data` / `text` / `event_id`, and so on) are valid **only for the duration of the callback** - the SDK never heap-allocates them. Copy them if they must be retained after the callback returns.
:::

#### Disconnect Reasons (`reason`) {#断连原因reason}

| Macro | Value | Description |
|----|---|------|
| `TAI_DISCONNECT_SESSION_CLOSE` | 0 | Server SessionClose; the Connection may remain alive (`connection_alive==1`) |
| `TAI_DISCONNECT_CONNECTION_CLOSE` | 1 | Server ConnectionClose; the worker stops |
| `TAI_DISCONNECT_TRANSPORT` | 2 | The worker detects a transport-layer failure |
| `TAI_DISCONNECT_PROTOCOL` | 3 | Fail-fast: parsing/behavior error |

#### `detail` (when `reason==TRANSPORT`) {#detail当-reasontransport}

| Macro | Value | Description |
|----|---|------|
| `TAI_TRANSPORT_PING_TIMEOUT` | 1 | Ping timeout |
| `TAI_TRANSPORT_EOF` | 2 | Peer closed the Connection (EOF) |
| `TAI_TRANSPORT_NET_ERROR` | 3 | Network error |

#### `detail` (when `reason==PROTOCOL`) {#detail当-reasonprotocol}

| Macro | Value | Description |
|----|---|------|
| `TAI_PROTO_ERR_BAD_VERSION` | 1 | Unknown leading Frame byte (desynchronization) |
| `TAI_PROTO_ERR_HMAC` | 2 | Frame HMAC verification failed |
| `TAI_PROTO_ERR_FRAME_DECODE` | 3 | Frame header decode failed |
| `TAI_PROTO_ERR_FRAG` | 4 | Orphan MIDDLE/LAST, overflow, or oversized data |
| `TAI_PROTO_ERR_PKT_DECODE` | 5 | Malformed application Packet / attribute block |
| `TAI_PROTO_ERR_UNKNOWN_PKT` | 6 | Unknown Packet type (strict mode) |
| `TAI_PROTO_ERR_EVENT` | 7 | Event unpacking failed / unknown event type |
| `TAI_PROTO_ERR_MEDIA_HDR` | 8 | Truncated media/text header |
| `TAI_PROTO_ERR_UNEXPECTED` | 9 | Valid Packet in an invalid state (behavior error) |
| `TAI_PROTO_ERR_OVERSIZED` | 10 | Inbound Frame exceeds rx_buf |

---

## 4. Lifecycle API {#4-生命周期-api}

### `tai_ctx_size` {#tai_ctx_size}

```c
size_t tai_ctx_size(void);
```

Returns the amount of memory required by the context. The caller must allocate at least this many bytes and pass the memory to `tai_ctx_init`.

---

### `tai_ctx_init` {#tai_ctx_init}

```c
tai_ctx_t *tai_ctx_init(void *mem, const tai_config_t *cfg);
```

Initializes the context. `mem` must be at least `tai_ctx_size()` bytes and remain valid for the lifetime of the context.

**Parameters:**
- `mem` - Preallocated memory block
- `cfg` - Configuration structure

**Return value:** Returns `tai_ctx_t*` (that is, `mem` cast to this type) on success, or NULL on failure.

---

### `tai_ctx_deinit` {#tai_ctx_deinit}

```c
void tai_ctx_deinit(tai_ctx_t *ctx);
```

Deinitializes the context and releases internal resources. `mem` can be freed after this call. It must be called after `tai_disconnect`.

---

### `tai_connect` {#tai_connect}

```c
int tai_connect(tai_ctx_t *ctx);
```

Performs the TLS handshake, sends ClientHello, establishes a Session, and starts the background receive thread.

**Return value:** `TAI_OK` on success, or `TAI_ERR_*` on failure.

**Behavior:**
- Blocks until the Connection is established or fails
- After success, the background thread begins processing Ping/Pong and receiving data
- The `tai_send_*` functions can be called once the Connection succeeds

---

### `tai_disconnect` {#tai_disconnect}

```c
void tai_disconnect(tai_ctx_t *ctx);
```

Stops and joins the background receive thread first, then sends a best-effort SessionClose, and finally closes the transport (TLS / TCP).

**Behavior:**
- Blocks until the background thread exits (join)
- Data cannot be sent after this call
- Call `tai_ctx_deinit` afterward
- **Must not** be called from a callback (it would join the worker thread that is executing the callback, causing a self-deadlock); to trigger disconnection from a callback, use `tai_request_disconnect`

---

### `tai_request_disconnect` {#tai_request_disconnect}

```c
void tai_request_disconnect(tai_ctx_t *ctx);
```

Requests the background worker to stop, but **does not** join it or tear down the Connection. It can be called from **any** thread, including from within a receive callback (unlike `tai_disconnect`). The worker exits on its next loop iteration.

**Purpose:** Safely triggers disconnection from a callback. After this call, the thread that owns the context must still call `tai_disconnect()` to join the worker, send SessionClose, and release resources.

---

## 5. Send API {#5-发送-api}

### `tai_send_text` {#tai_send_text}

```c
int tai_send_text(tai_ctx_t *ctx, const char *text, size_t len);
```

Sends a text message. Internally, one call sends four application Packets in sequence: `EventStart` -> text (with the `ONE_SHOT` stream flag) -> `EventPayloadsEnd` -> `EventEnd`; the caller does not need to finish the sequence manually.

**Parameters:**
- `text` - UTF-8 text
- `len` - Text length in bytes

**Return value:** `TAI_OK` or `TAI_ERR_*`.

---

### `tai_send_audio_start` {#tai_send_audio_start}

```c
int tai_send_audio_start(tai_ctx_t *ctx,
                         uint8_t codec, uint8_t channels,
                         uint8_t bit_depth, uint32_t sample_rate);
```

Starts an audio stream. It must be called before `tai_send_audio_chunk`.

:::note Server VAD mode
When Server VAD (continuous conversation) is enabled, call this function only once for the entire chat Session, not once per turn; keep the uplink audio stream open until the Session ends. See the "Server VAD vs. device-side VAD" comparison table in [VAD and Interruption](../guides/vad-and-interrupt).
:::

**Parameters:**
- `codec` - Encoding format: `TAI_AUDIO_PCM` (101) or `TAI_AUDIO_OPUS` (111)
- `channels` - Number of channels (usually 1)
- `bit_depth` - Bit depth (usually 16)
- `sample_rate` - Sample rate (usually 16000)

---

### `tai_send_audio_chunk` {#tai_send_audio_chunk}

```c
int tai_send_audio_chunk(tai_ctx_t *ctx, const uint8_t *pcm, size_t len);
```

Sends one chunk of audio data. It can be called repeatedly (for middle Packets in the stream).

**Parameters:**
- `pcm` - Audio data (a PCM or Opus frame, depending on the codec passed to `audio_start`)
- `len` - Data length in bytes

---

### `tai_send_audio_end` {#tai_send_audio_end}

```c
int tai_send_audio_end(tai_ctx_t *ctx);
```

Ends the audio stream. Notifies the server that the current audio input is complete and processing can begin.

:::warning Do not call in Server VAD mode
When Server VAD (`asr.enableVad`) is enabled, this function **must not** be called. The current cloud's turn-end signal is `TAI_EVT_CHAT_BREAK` (`TAI_EVT_SERVER_VAD` is no longer sent); do not call this function when that signal is received either. This function is only for manual push-to-talk or device-side local VAD stop detection. Calling it incorrectly actively ends the current uplink Event, causing the cloud to truncate the user's speech. See [VAD and Interruption](../guides/vad-and-interrupt).
:::

---

### `tai_send_image` {#tai_send_image}

```c
int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height);
```

Sends a single image.

**Parameters:**
- `data` - Image binary data
- `len` - Data length
- `format` - Image format: `TAI_IMG_JPEG` (1) or `TAI_IMG_PNG` (2)
- `width` / `height` - Image dimensions

---

### `tai_send_image_with_text` {#tai_send_image_with_text}

```c
int tai_send_image_with_text(tai_ctx_t *ctx,
                             const char *text, size_t text_len,
                             const uint8_t *img_data, size_t img_len,
                             uint8_t format,
                             uint16_t width, uint16_t height);
```

Sends text and an image together (for example, in an image-understanding scenario). Internally, the text Packet is sent before the image Packet to form one complete request.

---

### `tai_chat_break` {#tai_chat_break}

```c
int tai_chat_break(tai_ctx_t *ctx);
```

Sends a Chat break Event. This is used when the user actively interrupts the AI response (for example, with a button). After receiving it, the server stops generating the current response.

---

### `tai_send_mcp_response` {#tai_send_mcp_response}

```c
int tai_send_mcp_response(tai_ctx_t *ctx, const char *json_rpc_response);
```

Sends the response to an MCP command. When `on_event` receives `TAI_EVT_MCP_CMD`, the device executes the command and returns the result through this function.

**Parameters:**
- `json_rpc_response` - Response string in JSON-RPC format

---

## 6. Logging {#6-日志}

The SDK's global log switch is the compile-time `AGENTIC_KIT_LOG_LEVEL` (gated once in `common/log.h`; it applies to the entire SDK, not only this module); this module can additionally be lowered alone with `AGENTIC_KIT_TAI_LOG_LEVEL` (the renamed former `TAI_LOG_LEVEL` — lower-only, defaulting to the SDK-wide value). Valid values:

| Value | Meaning |
|----|------|
| 0 | Off entirely |
| 1 | `TAI_LOG_ERROR` |
| 2 | `TAI_LOG_WARN` |
| 3 | `TAI_LOG_INFO` |
| 4 | `TAI_LOG_DEBUG` (default) |

Log lines above the ceiling are eliminated at compile time; below it they emit unconditionally — there is no runtime level (`tai_set_log_level()` was removed together with the runtime layer). Compile production builds with `-DAGENTIC_KIT_LOG_LEVEL=2` to keep only error + warn, or lower only this module with `-DAGENTIC_KIT_TAI_LOG_LEVEL=N` (below 3, the `tai_log_packet` JSON formatter and its calls vanish together); changing where lines go (or dropping them by level) is a build decision as well: define `AGENTIC_KIT_LOG` and take over the dispatch with your own macro.

Media middle frames log at INFO with 1/N sampling by default (`AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N`, default 50; set it to 0 to disable sampling and enter flood mode). In flood mode middle frames emit at DEBUG and only when the effective ceiling is 4 — with a ceiling of 3, setting sampling to 0 leaves middle frames with **no logs at all** (fewer than the default sampling; do not mistake it for packet loss).

---

## 7. Typical Usage Flow {#7-典型使用流程}

```c
#include "tuya_ai.h"

// 1. Allocate memory
void *mem = malloc(tai_ctx_size());

// 2. Configure
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

// 3. Initialize + connect
tai_ctx_t *ctx = tai_ctx_init(mem, &cfg);
if (tai_connect(ctx) != TAI_OK) { /* handle error */ }

// 4. Send text
tai_send_text(ctx, "hello", 5);

// 5. Or send an audio stream
tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000);
while (has_audio) {
    tai_send_audio_chunk(ctx, pcm_frame, frame_len);
}
tai_send_audio_end(ctx);   // Finish in manual mode; do not call audio_end in Server VAD mode

// 6. Receive responses asynchronously through callbacks...

// 7. Clean up
tai_disconnect(ctx);
tai_ctx_deinit(ctx);
free(mem);
```

> The audio-stream call pattern depends on the VAD mode (continuous conversation with Server VAD vs. device-side VAD/manual push-to-talk). See the comparison table in [VAD and Interruption](../guides/vad-and-interrupt).

---

## 8. Thread Safety {#8-线程安全}

- The `tai_send_*` functions are thread-safe and can be called from any thread
- All callbacks are invoked sequentially on the same background receive thread
- `tai_send_*()` **can** be called from a callback (no lock is held)
- `tai_connect`, `tai_disconnect`, and `tai_ctx_deinit` **must not** be called from a callback (they join the worker thread, causing a self-deadlock)
- To trigger disconnection from a callback, call `tai_request_disconnect()`, then have the thread that owns the context call `tai_disconnect()`
