---
title: RTC Client SDK Reference
sidebar_label: RTC Client
sidebar_position: 2
---

# RTC Client SDK Reference

## 1. Overview {#1-概述}

### 1.1 SDK Introduction {#11-sdk-简介}

The **RTC Client SDK** (API prefix `stm_open_*`) is intended for external developers. It simplifies the workflow of the standard STM SDK, enabling rapid integration of AI capabilities without requiring an understanding of lower-level concepts such as Connect, Stream, and Event.

It is provided as a precompiled library and supports multiple platform architectures. For source-level integration or a simpler API, consider the [RTC TCP Client](./rtc-tcp-client).

---

### 1.2 Core Concepts {#12-核心概念}

#### 1.2.1 Session {#121-session会话}

**Definition:**  
A Session represents an **AI business session**, corresponding to an independent conversation or task context. When creating a Session, you must provide the **session_token** issued by the server; the SDK uses it internally to establish the connection and perform authentication.

**Lifecycle:**  
- **Create**: Call `stm_open_session_create(stm_open_session_config_t *config)` and provide client_type, token, id, encrypt_key, and callbacks (such as `on_state` and `on_data_recv`). On success, it returns `stm_open_session_t*`.  
- **Use**: Call `stm_open_session_send` multiple times on the same Session to send request data, and receive AI responses in `on_data_recv`.  
- **Close**: Call `stm_open_session_close(session)` to release resources when the business operation ends or the Session is no longer needed.

**Example:**  
A user opens a page for "Chat with AI" -> the application calls a platform interface to obtain the session_token and session_id -> the RTC Client SDK creates the Session -> when the user sends text or voice, the application sends it through `stm_open_session_send`, receives the AI response in `on_data_recv`, and displays it; when the user leaves the page, the application calls `stm_open_session_close`.

---

#### 1.2.2 Requests and Data Packets (event_id, fin) {#122-请求与数据包event_idfin}

**Definition:**  
A complete request-response exchange may consist of **multiple packets** (for example, a voice segment uploaded as multiple frames). The RTC Client SDK uses an **Event** to represent such a group of packets: packets belonging to the same request/response turn share the same **event_id**; the **first packet** can carry the data type and parameters (such as audio sample rate or image dimensions); and the **last packet** is marked with **fin=1**, allowing the foundation service and the device to agree that "this input segment has ended."

**Send side (when you call `stm_open_session_send`):**

- **event_id**: Event ID. Set it only in the **first packet** of the Event so the foundation service and business layer can associate multiple data packets with the same request.  
- **data_type**: Data type (such as text, audio, or image). Required in the first packet.  
- **payload / payload_length**: The data carried by this packet.  
- **fin**: **0** means more packets follow; **1** means this packet is the **last packet** of the Event.

**Receive side (`on_data_recv` callback):**

- **fin=0** means the current AI response has not ended; **fin=1** means this is the last packet of the response.  
- Use `data_type`, `payload`, and other fields in **data** to distinguish text, audio, images, and other data.

---

## 2. Prerequisites {#2-前期准备}

### 2.1 AI Agent Configuration {#21-智能体配置}

Create a product on the Tuya IoT Platform and bind or create an AI Agent. For details, see [Create an Agent](https://developer.tuya.com/en/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud). To customize a workflow, see [Create a Workflow](../guides/create-workflow).

### 2.2 Device Activation {#22-设备激活}

Activate the device through provisioning and obtain the device credentials (`devid`, `secret_key`, and `local_key`). For details, see the [tutorial](../tutorials/scan-by-device).

### 2.3 Obtain a Session Token {#23-获取-session-token}

Initialize the IoT Client by calling `iot_client_init()`, and then call `iot_client_get_session_token()` to obtain the `session_token`. For details, see the [IoT Client API](./iot-client).

---

## 3. SDK Initialization and Configuration {#3-sdk-初始化与配置}

### 3.1 Initialize the SDK (`stm_open_init`) {#31-sdk-初始化stm_open_init}

```c
stm_ret stm_open_init(stm_open_config_t *config);
```

Initializes the SDK and sets the log callback. It must be called before any other API and can be called only once during the SDK lifecycle.

**Configuration structure `stm_open_config_t`:**

| Field | Type | Description |
|------|------|------|
| `on_log` | `stm_log_cb_t` | Log callback function; can be NULL |

**Return values:** `STM_OK` on success; `STM_EINVALID_PARM` if a parameter is invalid.

---

### 3.2 Reset the SDK (`stm_open_reset`) {#32-sdk-重置stm_open_reset}

```c
stm_ret stm_open_reset(stm_open_config_t *config);
```

Resets the SDK to its initial state, releases all Sessions, and reapplies the configuration.

---

### 3.3 Deinitialize the SDK (`stm_open_deinit`) {#33-sdk-反初始化stm_open_deinit}

```c
void stm_open_deinit(void);
```

Releases all resources. After calling it, you must call `stm_open_init` again before using the SDK.

---

### 3.4 Get Version Information (`stm_open_get_version`) {#34-获取版本信息stm_open_get_version}

```c
uint32_t stm_open_get_version(void);
```

Returns the version number (major version in the high 8 bits, minor version in the middle 8 bits, and revision in the low 16 bits).

---

### 3.5 Configure Logging (`stm_open_set_log_level`) {#35-日志配置stm_open_set_log_level}

```c
stm_ret stm_open_set_log_level(stm_log_level_e level);
```

| Value | Macro | Description |
|--------|-----|------|
| 0 | `STM_LOG_LEVEL_VERBOSE` | Most detailed |
| 1 | `STM_LOG_LEVEL_DEBUG` | Debug |
| 2 | `STM_LOG_LEVEL_INFO` | Informational (recommended for production) |
| 3 | `STM_LOG_LEVEL_WARN` | Warning |
| 4 | `STM_LOG_LEVEL_ERROR` | Error |
| 5 | `STM_LOG_LEVEL_FATAL` | Fatal |
| 6 | `STM_LOG_LEVEL_NONE` | No output |

Note: rtc-client is a prebuilt library — its logs go through the `on_log` callback set at init, and the level is controlled by this runtime API, outside the SDK's compile-time `AGENTIC_KIT_LOG_LEVEL` / `AGENTIC_KIT_LOG` facade (modules compiled from source go through the facade; see Compile-Time Knobs).

---

## 4. Session Management {#4-会话管理}

### 4.1 Create a Session (`stm_open_session_create`) {#41-创建会话stm_open_session_create}

```c
stm_open_session_t* stm_open_session_create(stm_open_session_config_t *config);
```

**Configuration structure `stm_open_session_config_t`:**

| Field | Type | Description |
|------|------|------|
| `client_type` | `stm_client_type_e` | Client type |
| `session_token` | `char *` | Session token (cannot be NULL) |
| `session_id` | `char *` | Session ID (cannot be NULL) |
| `encrypt_key` | `char *` | Encryption key (cannot be NULL) |
| `on_state` | callback | State-change callback (can be NULL) |
| `on_data_recv` | callback | Data-receive callback (cannot be NULL) |
| `app_data` | `char *` | Application-defined data (can be NULL) |
| `user_data` | `void *` | User pointer passed through to callbacks |

**Return value:** A Session handle on success; NULL on failure.

---

### 4.2 Send Data (`stm_open_session_send`) {#42-发送数据stm_open_session_send}

```c
stm_ret stm_open_session_send(stm_open_session_t *session, stm_open_data_t *data, int8_t fin);
```

**Fields of `stm_open_data_t`:**

| Field | Type | Description |
|------|------|------|
| `event_id` | `char *` | Required in the first packet of an Event |
| `data_type` | `stm_data_type_e` | Data type |
| union | params | Set `audio_params`, `image_params`, or other type-specific parameters in the first packet |
| `payload` | `uint8_t *` | Data content |
| `payload_length` | `uint32_t` | Data length |
| `app_data` | `char *` | Application-defined data (can be NULL) |
| `timestamp` | `uint64_t` | Timestamp; valid only for video/audio/image types |

---

### 4.3 Close a Session (`stm_open_session_close`) {#43-关闭会话stm_open_session_close}

```c
void stm_open_session_close(stm_open_session_t *session);
```

---

## 5. Data Types {#5-数据类型}

| Value | Macro | Description |
|----|-----|------|
| 1 | `STM_DATA_TYPE_CMD` | System command (such as Chat break) |
| 2 | `STM_DATA_TYPE_VIDEO` | Video |
| 3 | `STM_DATA_TYPE_AUDIO` | Audio |
| 4 | `STM_DATA_TYPE_IMAGE` | Image |
| 5 | `STM_DATA_TYPE_FILE` | File |
| 6 | `STM_DATA_TYPE_TEXT` | Text |

### Audio Parameters (`stm_audio_params_t`) {#音频参数stm_audio_params_t}

| Field | Type | Description |
|------|------|------|
| `codec_type` | `uint16_t` | Codec type (101=PCM, 111=OPUS) |
| `sample_rate` | `uint32_t` | Sample rate in Hz |
| `channels` | `uint16_t` | Number of channels |
| `bit_depth` | `uint16_t` | Bit depth |
| `container` | `uint16_t` | Audio container type |
| `bitrate` | `uint32_t` | Bit rate |
| `frame_duration` | `uint16_t` | Frame duration in ms |
| `frame_size` | `uint16_t` | Frame size in bytes |

### Image Parameters (`stm_image_params_t`) {#图像参数stm_image_params_t}

| Field | Type | Description |
|------|------|------|
| `payload_type` | `uint8_t` | 0=raw, 1=base64, 2=url |
| `format` | `uint8_t` | 1=JPEG, 2=PNG |
| `width` | `uint16_t` | Width |
| `height` | `uint16_t` | Height |

---

## 6. Error Codes {#6-错误码}

| Value | Macro | Description | Recommended action |
|----|-----|------|----------|
| 0 | `STM_OK` | Success | - |
| -1 | `STM_ENOT_INIT` | Not initialized | Call `stm_open_init` first |
| -4 | `STM_EINVALID_PARM` | Invalid parameter | Check pointers and fields |
| -5 | `STM_ENOT_CONNECTED` | Not connected | Wait until on_state reports ready |
| -6 | `STM_ECONNECTION_CLOSED` | Connection closed | Obtain a new token and establish a Session again |
| -9 | `STM_EAUTH_FAILED` | Authentication failed | Check the token/key |
| -15 | `STM_ETOKEN_EXPIRED` | Token expired | Obtain a new session_token |
| -21 | `STM_EBUFFER_NOT_ENOUGH` | Insufficient buffer | Reduce the packet size (no more than 200KB recommended) |
| -29 | `STM_ESSL_HANDSHAKE_FAILED` | TLS handshake failed | Check time/certificate/network |

For the complete list of error codes, see the `stm_errno.h` header.

---

## 7. Quick Examples {#7-快速示例}

### Text Chat {#文本聊天}

```c
stm_open_config_t config = { .on_log = my_log_cb };
stm_open_init(&config);

stm_open_session_config_t sess_cfg = {
    .client_type   = STM_CLIENT_TYPE_DEVICE,
    .session_token = token,
    .session_id    = sid,
    .encrypt_key   = local_key,
    .on_data_recv  = on_recv,
};
stm_open_session_t *sess = stm_open_session_create(&sess_cfg);

stm_open_data_t d = {0};
d.event_id       = "t1";
d.data_type      = STM_DATA_TYPE_TEXT;
d.payload        = (uint8_t *)"hello";
d.payload_length = 5;
stm_open_session_send(sess, &d, 1);

// ... wait for on_recv callbacks ...
stm_open_session_close(sess);
stm_open_deinit();
```

### Audio Chat {#音频聊天}

```c
// First packet: includes audio parameters
stm_open_data_t first = {0};
first.event_id   = "a1";
first.data_type  = STM_DATA_TYPE_AUDIO;
first.audio_params = (stm_audio_params_t){
    .codec_type  = 101,   // PCM
    .sample_rate = 16000,
    .channels    = 1,
    .bit_depth   = 16,
};
first.payload        = pcm_chunk1;
first.payload_length = chunk_len;
stm_open_session_send(sess, &first, 0);

// Middle packet
stm_open_data_t mid = {0};
mid.data_type      = STM_DATA_TYPE_AUDIO;
mid.payload        = pcm_chunk2;
mid.payload_length = chunk_len;
stm_open_session_send(sess, &mid, 0);

// Last packet
stm_open_data_t last = {0};
last.data_type      = STM_DATA_TYPE_AUDIO;
last.payload        = pcm_chunk3;
last.payload_length = chunk_len;
stm_open_session_send(sess, &last, 1);  // fin=1
```

---

## 8. Comparison with RTC TCP Client {#8-与-rtc-tcp-client-的对比}

| | RTC Client (`stm_open_*`) | RTC TCP Client (`tai_*`) |
|---|---|---|
| Integration | Precompiled static library | Source code (portable through the PAL) |
| Protocol | tRTC (Tuya's proprietary RTC protocol), implemented over UDP | tRTC (Tuya's proprietary RTC protocol), implemented over TCP |
| Session management | Requires a session_token (obtained from the IoT Client) | Connects directly using device_id + local_key |
| API style | Generic send/recv + data structure | Typed APIs (`send_text`, `send_audio_*`) |
| MCP support | No dedicated type (`stm_cmd_type_e` defines only the Chat break command) | Native `TAI_EVT_MCP_CMD` + `tai_send_mcp_response` |
| Platform support | macOS/Linux/MIPS/ARM (precompiled) | Any platform that implements the PAL |
| Best suited for | Rapid integration on an existing platform | New projects, control over source code, ESP-IDF |
