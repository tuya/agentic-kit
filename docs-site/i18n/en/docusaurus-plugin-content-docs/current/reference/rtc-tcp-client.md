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
| `TAI_DATA_ID_IMAGE_DOWN` | 6 | Downlink image (advertised by keyed Sessions) |
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

Populate this structure before calling `tai_ctx_init`. All pointer fields except `image_key` must remain valid for the lifetime of the context; that key is copied during successful construction.

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
| `sign_level` | `uint8_t` | Without `image_key`: `TAI_SIGN_HMAC_SHA256` (recommended) or `TAI_SIGN_HMAC_SHA1`; 0 means unset and falls back to HMAC-SHA256. With `image_key`, this field is ignored: sign level is 0 and no Frame carries a signature. |

#### Image key and encrypted-media Connections {#image-decrypt-key}

The legacy anchor `image-decrypt-key` is retained for existing links; the configuration field is `image_key`, and the same key encrypts uplink and decrypts downlink images.

| Field | Type | Description |
|------|------|------|
| `image_key` | `const uint8_t *` | Optional fixed AES-128 image key: exactly `TAI_IMAGE_KEY_LEN` (16) decoded binary bytes. NULL preserves legacy behavior. |

:::warning A supplied key requires TLS
`image_key` enables TuyaOpen's encrypted-media profile: Frame version 3, sign level 0 (ignoring `cfg.sign_level`), with the v2 Attribute layout unchanged. The 36-byte ClientHello suite is `00 || random[32] || 04 03 03`; the last three bytes select AES-128-GCM, whole video I-frames and whole images as encryption ranges, **not a media-capability bitmap**. This SDK does not support video or file handling.

The user explicitly approved this unsigned-Frame security decision. Image GCM uses no AAD and authenticates the encrypted body of each complete Image Packet; metadata, control Packets, URLs and empty markers rely on TLS protection. A nonempty `cfg.ca_cert` or `cert_bundle_attach` verifies the peer; with neither, a keyed Connection still uses TLS but with the peer unverified (one warning at connect) -- the same CA-driven rule as the IoT/MQTT path. With a key, `disable_tls` makes `tai_connect()` return `TAI_ERR_TLS` before networking (raw TCP is unsupported). Unverified TLS risks man-in-the-middle attacks exposing tokens and the image key and compromising metadata and control traffic, so omit a trust source only outside production. The implementation is enabled, but this is not a live-cloud verification claim.
:::

Keyed Sessions advertise only audio/text/image: send IDs 1/3/5 and receive IDs 2/4/6. TuyaOpen allocates even receive IDs in configured type order; that supports this SDK's Session allocation, not a universal fixed ID for each media type. Keep `protocol_version` at 0 or `TAI_VER_21`; do not set it to 3 to select Frame version 3.

- Pass binary key material, not Base64 text, a C string, `local_key`, device credentials or a derived Frame key. The pointer must reference at least 16 readable bytes; the application must validate the decoded length. An all-zero key is structurally valid and still counts as supplied.
- `tai_ctx_init()` copies the key only on success. The caller may then wipe/release its temporary bytes. The context owns an immutable copy, retained across disconnect and failed Connection attempts, and `tai_ctx_deinit()` wipes it after the owner has stopped/joined any worker with `tai_disconnect()`.
- The key is fixed. Acquisition and optional retention belong to the application; there is no automatic acquisition, retry, refresh, rotation or persistence in IoT initialization or any TAI lifecycle/receive path. Reconnect reuses the same key without another ATOP call.
- Rebuild the SDK and consumers of the public `tai_config_t` / `tai_image_msg_t` layouts against the same headers. Allocate opaque context storage using `tai_ctx_size()`. IoT public layouts (`iot_client_config_t`, `iot_on_boarding_config_t`, `iot_client_t`) are unchanged.

See [explicit application acquisition](#application-image-key) for a construction-only example.

### 3.6 Keepalive Configuration {#36-保活配置}

| Field | Type | Description |
|------|------|------|
| `ping_interval_ms` | `uint32_t` | Ping interval (0 = default 60000ms) |
| `ping_timeout_ms` | `uint32_t` | Receive-liveness timeout refreshed by any inbound data (0 = default 90000ms); intentional backpressure suspends it and resume grants a fresh full budget |
| `connect_timeout_ms` | `uint32_t` | Connection timeout (0 = default 5000ms). It separately constrains the two sequential wait stages of `tai_connect`: first Connection establishment (TCP connection + TLS handshake, sharing one budget), then the server's SessionNew response. A timeout in either stage means the Connection failed, so the worst-case duration of `tai_connect` is approximately twice this value. |

### 3.7 Test Configuration {#37-测试配置}

| Field | Type | Description |
|------|------|------|
| `disable_tls` | `uint8_t` | If nonzero, skips TLS for no-key integration tests only; with an image key, `tai_connect()` returns `TAI_ERR_TLS` before networking |

### 3.8 Platform Adaptation {#38-平台适配}

| Field | Type | Description |
|------|------|------|
| `pal` | `const pal_t *` | Pointer to the platform adaptation layer implementation |
| `cert_bundle_attach` | `tls_cert_bundle_attach_fn` | Platform certificate bundle callback (set to `(tls_cert_bundle_attach_fn)esp_crt_bundle_attach` on ESP-IDF); NULL means no bundle. Provides peer verification as an alternative to `ca_cert`. See [TLS Certificate Verification](../guides/tls-cert-verification.md) |
| `ca_cert` | `const char *` | Optional CA certificate: full PEM or bare Base64 body, **not a file path**. Borrowed until context destruction; nonempty requires peer verification. A keyed Connection verifies the peer only when this field or `cert_bundle_attach` is set; omitting both still uses TLS but with the peer unverified (one warning at connect). |

### 3.9 Callbacks {#39-回调函数}

All callbacks are invoked on the background receive thread.

| Field | Type | Description |
|------|------|------|
| `on_audio` | function pointer | Audio data callback |
| `on_text` | function pointer | Text data callback |
| `on_image` | function pointer | Image data callback (images generated by the cloud) |
| `on_event` | function pointer | Event callback (MCP commands, Chat break, VAD, and so on) |
| `on_disconnect` | function pointer | Disconnect callback |
| `on_flow_control` | `int (*)(tai_ctx_t *, void *)` | Optional receive-backpressure hook; nonzero admits receive, 0 pauses reads and parsing, NULL disables backpressure |
| `user_data` | `void *` | Passed through to all callbacks |

**Callback signatures:**

Each callback receives a **single** pointer to a `const` message structure plus `user_data` (instead of the former multi-argument form).

```c
void (*on_audio)     (tai_ctx_t *ctx, const tai_audio_msg_t      *msg, void *user_data);
void (*on_text)      (tai_ctx_t *ctx, const tai_text_msg_t       *msg, void *user_data);
void (*on_image)     (tai_ctx_t *ctx, const tai_image_msg_t      *msg, void *user_data);
void (*on_event)     (tai_ctx_t *ctx, const tai_event_msg_t      *msg, void *user_data);
void (*on_disconnect)(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *user_data);
int  (*on_flow_control)(tai_ctx_t *ctx, void *user_data);
```

#### Receive Backpressure (`on_flow_control`)

- The worker calls the hook before each read and between complete Frames. A conforming server never consults it during the connect handshake (the ack precedes media); only a server coalescing media ahead of the ack could. The hook must not block; the application synchronizes shared queue state.
- Returning 0 pauses both reads and parsing, stalling all inbound traffic including ChatBreak, ASR text, Pong, and EOF detection. Resume processes complete buffered Frames before reading again; partial input returns to bounded blocking receive.
- An intentional pause suspends receive-liveness timeout accounting, and resume grants a fresh `ping_timeout_ms` budget. Ping and stop requests remain active; Ping send failure still disconnects.
- Admission is also checked before each codec-frame callback within an Audio Packet. A mid-Packet pause retains that Packet's remaining bytes without copying; resume delivers the remaining frames before any later Packet. Applications discard obsolete audio inside `on_audio` by comparing the server timestamp (`msg->timestamp_ms`), not by mutating SDK receive state.

Backpressure is rechecked every `AGENTIC_KIT_TAI_FLOW_CONTROL_POLL_MS` (50 ms by default), and sustained receive traffic yields CPU for `AGENTIC_KIT_TAI_WORKER_YIELD_MS` (10 ms by default).

`pal_t` adds a mandatory `sleep_ms` callback. It must sleep without busy-waiting for at least the requested milliseconds, treat 0 as a no-op, and not depend on socket readability. Recompile all custom PALs and consumers against the updated header.

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
| `timestamp_ms` | `uint64_t` | Server media-header timestamp, **not** local time; latch START's value to filter a stream against an interruption time |

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
| `data` | `const uint8_t *` | Borrowed image payload: authenticated plaintext or URL pass-through with a key, legacy raw payload without one; valid only during the callback |
| `payload_type` | `uint8_t` | With a key: `TAI_IMG_PAYLOAD_RAW` after decryption, `TAI_IMG_PAYLOAD_URL` for URL pass-through; the legacy path reports the type parsed for that Packet, defaulting to RAW |
| `len` | `size_t` | Data length in bytes |
| `format` | `uint8_t` | `TAI_IMG_JPEG` / `TAI_IMG_PNG` / 0=unknown |
| `width` | `uint16_t` | Width (px), 0 if unknown or this Packet omits image-params |
| `height` | `uint16_t` | Height (px), 0 if unknown or this Packet omits image-params |
| `stream_flag` | `uint8_t` | `TAI_STREAM_*` |
| `data_id` | `uint16_t` | Data ID (from the media header); keyed receive requires `TAI_DATA_ID_IMAGE_DOWN` (6) |
| `event_id` | `const char *` | Turn ID (borrowed); `""` if absent |
| `timestamp_ms` | `uint64_t` | Stream start timestamp (media header) |

:::note Streaming reception
Received images arrive as a stream of chunks: START (or ONE_SHOT) carries the first bytes and image-params, MIDDLE continues the stream, and END finishes it (len may be 0). The caller copies/accumulates chunks according to `stream_flag`; image-codec decoding after END or ONE_SHOT is appropriate only if the application knows the accumulated bytes form a supported image file. `format`/`width`/`height` come from that Packet's image-params and are 0 for MIDDLE/END that omit the Attribute.
:::

The callback signature and worker-thread delivery are unchanged; the added `payload_type` identifies the delivered payload. The implemented keyed path authenticates each complete Image Packet after transport Fragmentation reassembly: the 24-byte header includes a 16-byte IV; RAW bodies contain ciphertext plus a 16-byte GCM tag, while Base64 bodies are strictly decoded before authentication; AES-128-GCM uses no AAD. Nonempty image plaintext is delivered only after authentication succeeds; tag-only bodies must also authenticate. Header-only 24-byte START/ONE_SHOT/END Packets are zero-length markers; header-only MIDDLE is rejected. Markers are not authenticated image data. URLs are typed pass-through only, with no GCM or download. Without a key, original bytes remain unchanged, with no automatic decoding or decryption. The SDK does not decode JPEG/PNG into pixels.

Keyed parsing strictly validates the four unsigned image-params fields and their ranges, duplicate image-params and Event IDs. START/ONE_SHOT requires image-params; MIDDLE/END must match the latched Event/Data ID and any repeated parameters must agree. Continuations without Attributes inherit payload type but report 0 for `format`/`width`/`height`; matching repeated Attributes supply those values. Orphan/conflicting continuations, malformed Base64, truncation and authentication failures do not deliver the failed Packet. Completion, Event boundaries, SessionClose, disconnect and errors clear the relevant stream state, without key acquisition or plaintext fallback.

Plaintext uses temporary PAL heap storage, wiped and freed after callback return; copy `len` bytes to retain it. URL/text bytes need not be NUL-terminated. The existing `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` default of 32,000 bytes limits one reassembled Packet, not an entire image. RAW needs separate plaintext scratch; Base64 also needs decode scratch. Maximum cloud Packet sizes and target peak memory are **not measured**. Keyed Connections are enabled subject to the [TLS trust requirements and explicit exception](#image-decrypt-key).

`tai_event_msg_t` (Event callback):

| Field | Type | Description |
|------|------|------|
| `event_type` | `uint16_t` | `TAI_EVT_*` |
| `data` | `const uint8_t *` | Event payload (usually JSON) |
| `len` | `size_t` | Payload length in bytes |
| `event_id` | `const char *` | attr 61 (borrowed); `""` if absent |
| `user_data` | `const uint8_t *` | attr 111 (borrowed), **not** NUL-terminated; NULL if absent |
| `user_data_len` | `size_t` | `user_data` length in bytes; separate from the event payload, and the SDK does not parse its JSON |

:::note ChatBreak interruption time
The server time for `TAI_EVT_CHAT_BREAK` is not in the event payload but in attr 111: `{"breakAttributes":{"time":"<server-time>"}}`, a server epoch in milliseconds — the same clock and unit as `on_audio`'s `timestamp_ms`. Read `msg->user_data`, parse `breakAttributes.time`, and compare it with the `timestamp_ms` latched in `on_audio` to decide whether a stream is obsolete. The MQTT `asrInterrupt` arrives by a different path: its time is the `time` field of its own payload, and it is the same server time value.
:::

`tai_disconnect_msg_t` (disconnect callback):

| Field | Type | Description |
|------|------|------|
| `reason` | `uint8_t` | `TAI_DISCONNECT_*` (see the table that follows) |
| `close_code` | `uint16_t` | Server close code (SESSION/CONNECTION); 0 in other cases |
| `detail` | `uint8_t` | `TAI_TRANSPORT_*` / `TAI_PROTO_ERR_*` (see the tables that follow); 0 in other cases |
| `connection_alive` | `uint8_t` | 1 only when `reason==SESSION_CLOSE` |
| `session_id` | `char[64]` | Copied value; `""` if absent |

:::warning Lifetime
`msg` and all pointers within it (`data` / `text` / `event_id`, and so on) are valid **only for the duration of the callback** - image plaintext may point to temporary heap storage that is wiped and freed after callback return. Copy them if they must be retained after the callback returns.
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
| `TAI_PROTO_ERR_IMAGE_AUTH` | 11 | Image GCM authentication failed |
| `TAI_PROTO_ERR_IMAGE_PROCESSING` | 12 | Image scratch allocation or processing failed |

These are positive fatal details, not negative `TAI_ERR_*` return codes. Image errors produce one terminal `on_disconnect(PROTOCOL, detail)`; malformed headers/Attributes and invalid stream state still use the corresponding existing details. The failed Packet produces no `on_image`; the application must discard any previously accumulated incomplete image.

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

Performs the TLS handshake, sends ClientHello, confirms the Session acknowledgement, and starts the background receive thread. Supplying `image_key` at construction selects the [encrypted-media profile](#image-decrypt-key), rather than blocking the Connection. Raw TCP returns `TAI_ERR_TLS` before networking. A nonempty `ca_cert` or `cert_bundle_attach` requires peer verification; with neither, the Connection still uses TLS but with the peer unverified and one warning logged. Certificate or TLS handshake failure also returns `TAI_ERR_TLS`. No-key behavior remains unchanged.

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
When Server VAD (continuous conversation) is enabled, call this function only once for the entire chat Session, not once per turn; keep the uplink audio stream open until the Session ends. See the "Server VAD vs. device-side VAD" comparison table in [VAD and Interruption](../guides/vad-and-interrupt.md).
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
When Server VAD (`asr.enableVad`) is enabled, this function **must not** be called. The current cloud's turn-end signal is `TAI_EVT_CHAT_BREAK` (`TAI_EVT_SERVER_VAD` is no longer sent); do not call this function when that signal is received either. This function is only for manual push-to-talk or device-side local VAD stop detection. Calling it incorrectly actively ends the current uplink Event, causing the cloud to truncate the user's speech. See [VAD and Interruption](../guides/vad-and-interrupt.md).
:::

---

### `tai_send_image` {#tai_send_image}

```c
int tai_send_image(tai_ctx_t *ctx,
                   const uint8_t *data, size_t len,
                   uint8_t format, uint16_t width, uint16_t height);
```

Sends a single image.

`image_key` is shared by all three image send APIs: `tai_send_image()`, `tai_send_image_with_text()` and `tai_send_image_audio_start()`. Encryption preparation completes before EventStart without modifying the caller's buffer. A nonempty image needs `len + 32` bytes of temporary scratch (16-byte IV + `len` ciphertext bytes + 16-byte tag); an empty image needs 16 scratch bytes (IV only, no tag). Scratch is wiped and freed after send success or failure; no-key sends retain legacy behavior. These send paths use the [verified-TLS encrypted-media Connection](#image-decrypt-key); live-cloud compatibility remains unverified.

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

> The audio-stream call pattern depends on the VAD mode (continuous conversation with Server VAD vs. device-side VAD/manual push-to-talk). See the comparison table in [VAD and Interruption](../guides/vad-and-interrupt.md).

---

### Explicit application image-key acquisition (construction only) {#application-image-key}

Use the public generic `iot_atop_call()` with API `thing.km.secret.get.device`, version `1.0`, and `data = "{}"`. The empty object follows the TuyaOpen reference's lack of business parameters and is covered by the local mock tests only; **actual cloud acceptance is UNVERIFIED**. The generic API does not add business fields; do not invent token/agent parameters. `OPRT_OK` alone does not establish that `result.secretValue` is usable.

This application-local example returns a constructed context or NULL, **never calls `tai_connect()`**, and makes one explicit ATOP request with no retries. Supply an activated IoT client after `iot_init()`, suitably aligned caller-owned `mem` of at least `tai_ctx_size()` bytes, and an otherwise complete `base` configuration. The `ca_cert` argument must supply a real application-trusted CA (full PEM or bare Base64 body), or be NULL when using an existing platform bundle in `base->cert_bundle_attach`. The helper assigns the CA to `cfg.ca_cert` rather than embedding an example certificate; keep the CA and all other borrowed fields alive for the context lifetime. The example uses only public SDK/library headers; it adds no named SDK wrapper and does not change cJSON's allocator hooks.

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

The validator accepts only canonical standard Base64: 24 characters, exactly 22 alphabet characters followed by `==`, zero unused bits in the last sextet, and exactly 16 decoded bytes. It rejects whitespace, URL-safe variants, bad padding and wrong lengths before configuration. Transport/business failures, absent/null/non-object results, missing/non-string secrets, invalid Base64 and constructor failure all return NULL through the same cleanup path.

The temporary binary key is zeroized after construction on success and on every failure. Before deleting the parsed tree, the local helper traverses every node through public child/next fields and wipes string values and object member names, including nested content in rejected shapes, not just secretValue. Returned plaintext JSON and response.error_code/error_msg are also wiped before response cleanup. This is practical cleanup of application-visible buffers, not a guarantee that parser/transport internals leave no other copies. Do not log the response, `secretValue` or decoded key. Ensure the SDK's secret-API response-body log suppression is present before using real keys; disabling only TAI logs does not suppress IoT ATOP diagnostics.

On success the caller owns the context: eventually call `tai_disconnect(ctx)`, then `tai_ctx_deinit(ctx)`, then release `mem`. On NULL, no constructed context is returned and the caller still owns `mem`. This example does not persist the fixed key. An application that already retains a validated key can pass it directly to `tai_ctx_init()` without another request. Both paths select the encrypted-media profile. After successful construction, the owner calls `tai_connect(ctx)` and checks the result; encrypted images can be sent and received after TLS peer verification and Session confirmation succeed. The helper only checks that trust is configured; actual certificate verification happens at connect time. This is not a live-cloud verification claim. The earlier typical flow omits the field (NULL) and remains legacy behavior.

---

### Generate an image and save it locally {#image-generation-demo}

`examples/posix/ai/rtc-tcp-client/image_generation_demo.c` sends one prompt with `tai_send_text()` and saves the first complete inline PNG/JPEG. This is cloud text-to-image generation, not an image upload for recognition or local rendering. It requires an activated device bound to an authorized Agent whose cloud workflow generates **inline Image Packets**. Follow the product's [workflow configuration](../guides/create-workflow.md) and routing contract; sending a prompt does not configure a workflow, and `clm_intent=ai_image` is not assumed to be a universal generation intent.

Supply `TUYA_DEVICE_ID`, `TUYA_SECRET_KEY`, and `TUYA_LOCAL_KEY` externally through a secure environment/secret-injection mechanism before running. Do not commit credentials or type literal secrets into shell commands/history. By default, `--ca-file` is required: provide a trusted CA bundle covering **both bootstrap HTTPS and TAI endpoints**. The demo reads its contents (nonempty, at most 256 KiB) into IoT `cacert` and TAI `ca_cert`, retaining them through client teardown; the SDK receives certificate contents, not the filename. `--allow-insecure-fallback` explicitly permits omitting the CA file, leaving TLS peer verification disabled with a MITM warning. A supplied CA is always verified, even with that flag; certificate failures never trigger an unverified retry. The demo does not download trust anchors over an unverified connection.

:::warning Rebuild all SDK libraries with a WARN ceiling before using credentials
Existing SDK DEBUG logs can expose the Session-token response, and TAI Packet logs at INFO may expose tokens. Use `-DCMAKE_C_FLAGS="-DAGENTIC_KIT_LOG_LEVEL=2"` as below so **all C libraries**, not just the demo or TAI, compile with only ERROR/WARN logs. A bare CMake `-DAGENTIC_KIT_LOG_LEVEL=2` variable is not wired to compiler definitions. There is no runtime log-level switch; changing the ceiling requires rebuilding.
:::

From the repository root, use the separate `build-examples` directory (do not reuse the root SDK build directory):

```sh
cmake -S examples/posix -B build-examples \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_C_FLAGS="-DAGENTIC_KIT_LOG_LEVEL=2"
cmake --build build-examples --target image_generation_demo -j 4

./build-examples/image_generation_demo \
  --prompt "Generate a watercolor picture of a red panda reading under a tree" \
  --ca-file /path/to/trusted-ca-bundle.pem \
  --output-dir ./generated-images \
  --timeout 120 \
  --region AY
```

Replace the CA path with your configuration. Each product binds one Agent, and all devices belonging to that product use that Agent; the demo does not offer Agent selection. The demo uses **PROD only**, defaults to **AY**, and accepts the exact region spellings `AY`, `AZ`, `UEAZ`, `EU`, `WEAZ`, `IN`, `SG`. `--timeout` defaults to **120 seconds**, measured after sending the prompt; it excludes bootstrap HTTPS and Connection setup, so it is not a total execution deadline.

- **Bootstrap and key lifetime:** With MQTT auto-connect disabled, the demo obtains a Session token and [explicitly acquires the fixed Image key](#application-image-key) once. It validates canonical Base64 decoding to 16 bytes, wipes application-owned key-response copies and the temporary key after construction, and wipes the context's key at teardown. It neither persists the key nor reconnects/resubmits the prompt; replay could duplicate generation or charges.
- **Bounded reception:** Callback payloads/identifiers are borrowed and copied before return into one mutex-protected accumulator, capped at **16 MiB** for the entire image (SDK per-Packet limits still apply). Completion requires a nonempty ONE_SHOT or a completed START/MIDDLE/END stream, not merely EventEnd. Callbacks request shutdown with `tai_request_disconnect()`; only the main thread writes files, after `tai_disconnect()` joins the worker.
- **Local output:** `--output-dir` defaults to `./generated-images` relative to the process working directory. The demo validates it before cloud calls and creates only the final directory if missing; its parent must already exist. PNG/JPEG signature checks reject unknown bytes or a mismatch with a nonzero declared format; this is not decoder validation. An exclusive temporary file is written and closed, then published with a unique locally generated `.png`/`.jpg` name without overwriting existing files; collisions fail. Prompts and Event IDs are not filenames.
- **Result and unsupported delivery:** A generation run exits 0 only after successful publication and prints the saved path, byte count, format and dimensions (0 means unknown). Text/audio are discarded. Image URL delivery and downloading image links from NLG text are not implemented; URL-only or text-only responses fail rather than being saved as images. Timeout, interruption, incomplete images, bootstrap/TLS/protocol errors, size/allocation failures and file errors return nonzero.

Live-cloud generation, the selected workflow's delivery mode, key-service acceptance and encrypted-media interoperability remain **unverified**; this usage example is not evidence of an end-to-end cloud run.

---

## 8. Thread Safety {#8-线程安全}

- The `tai_send_*` functions are thread-safe and can be called from any thread
- All callbacks are invoked sequentially on the same background receive thread
- `tai_send_*()` **can** be called from a callback (no lock is held)
- `tai_connect`, `tai_disconnect`, and `tai_ctx_deinit` **must not** be called from a callback (they join the worker thread, causing a self-deadlock)
- To trigger disconnection from a callback, call `tai_request_disconnect()`, then have the thread that owns the context call `tai_disconnect()`
