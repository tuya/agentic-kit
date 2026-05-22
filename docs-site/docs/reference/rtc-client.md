---
title: RTC Client SDK 参考文档
sidebar_label: RTC Client
sidebar_position: 2
---

# RTC Client SDK 参考文档

## 1. 概述

### 1.1 SDK 简介

**RTC Client SDK**（API 前缀 `stm_open_*`）面向外部开发者，在标准 STM SDK 的基础上做了流程简化，便于快速接入 AI 能力，无需理解 Connect、Stream、Event 等底层概念。

以预编译库形式提供，支持多种平台架构。如需源码级集成或更简洁的 API，请考虑 [RTC TCP Client](./rtc-tcp-client)。

---

### 1.2 核心概念

#### 1.2.1 Session（会话）

**定义：**  
Session 表示**AI业务会话**，对应一个独立的对话或任务上下文。创建 Session 时需传入由服务端下发的 **session_token**；SDK 内部会据此完成连接与鉴权。

**生命周期：**  
- **创建**：调用 `stm_open_session_create(stm_open_session_config_t *config)`，传入 client_type、token、id、encrypt_key 及回调（如 `on_state`、`on_data_recv`），成功则返回 `stm_open_session_t*`。  
- **使用**：在同一 Session 上多次调用 `stm_open_session_send` 发送请求数据，在 `on_data_recv` 中接收 AI 返回。  
- **关闭**：业务结束或不再需要该会话时调用 `stm_open_session_close(session)` 释放资源。

**举例：**  
用户打开一个「与 AI 对话」的页面 → 应用调用平台接口获取 session_token 和 session_id → 用 RTC Client SDK 创建该 Session → 用户发送文字或语音时通过 `stm_open_session_send` 发送，在 `on_data_recv` 里收到 AI 的回复并展示；用户离开页面时调用 `stm_open_session_close`。

---

#### 1.2.2 请求与数据包（event_id、fin）

**定义：**  
一次完整的「请求-响应」在数据上可能由**多包**组成（例如一段语音拆成多帧上传）。RTC Client SDK 用 **事件** 来刻画这样一组包：同一轮请求/返回包共享同一个 **event_id**，**首包**可携带数据类型和参数（如音频采样率、图像宽高），**末包**通过参数 **fin=1** 标记，便于基座和端侧对齐「这一段输入已结束」。

**发送侧（你调用 `stm_open_session_send` 时）：**

- **event_id**：事件 ID，仅在本事件的**第一包**里填写，用于基座和业务层关联同属一次请求的多个数据包。  
- **data_type**：数据类型（如文本、音频、图像等），首包时必填。  
- **payload / payload_length**：本包的数据内容。  
- **fin**：**0** 表示后面还有包，**1** 表示本包是该事件的**最后一包**。

**接收侧（`on_data_recv` 回调）：**

- **fin=0** 表示本轮 AI 返回还未结束；**fin=1** 表示本条响应的最后一包。  
- 通过 **data** 中的 `data_type`、`payload` 等区分文本、音频、图片等。

---

## 2. 前期准备

### 2.1 智能体配置

在 Tuya IoT 平台上创建产品并绑定或创建 AI Agent，详见 [创建 Agent](https://developer.tuya.com/cn/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud)。如需自定义工作流，可参考[创建工作流](../guides/create-workflow)。

### 2.2 设备激活

通过配网操作激活并获取设备凭证（`devid`、`secret_key`、`local_key`）。详见[教程](../tutorials/scan-by-device)。

### 2.3 获取 session token

通过 `iot_client_init()` 初始化 IoT 客户端，再调用 `iot_client_get_session_token()` 获取 `session_token`。详见 [IoT Client API](./iot-client)。

---

## 3. SDK 初始化与配置

### 3.1 SDK 初始化（`stm_open_init`）

```c
stm_ret stm_open_init(stm_open_config_t *config);
```

初始化 SDK，设置日志回调。必须在调用其他 API 之前调用，生命周期内只能调用一次。

**配置结构体 `stm_open_config_t`：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `on_log` | `stm_log_cb_t` | 日志回调函数，可为 NULL |

**返回值：** `STM_OK` 成功，`STM_EINVALID_PARM` 参数无效。

---

### 3.2 SDK 重置（`stm_open_reset`）

```c
stm_ret stm_open_reset(stm_open_config_t *config);
```

重置到初始状态，释放所有会话，重新应用配置。

---

### 3.3 SDK 反初始化（`stm_open_deinit`）

```c
void stm_open_deinit(void);
```

释放所有资源。调用后需重新 `stm_open_init` 才能再次使用。

---

### 3.4 获取版本信息（`stm_open_get_version`）

```c
uint32_t stm_open_get_version(void);
```

返回版本号（高 8 位主版本、中 8 位次版本、低 16 位修订号）。

---

### 3.5 日志配置（`stm_open_set_log_level`）

```c
stm_ret stm_open_set_log_level(stm_log_level_e level);
```

| 级别值 | 宏 | 说明 |
|--------|-----|------|
| 0 | `STM_LOG_LEVEL_VERBOSE` | 最详细 |
| 1 | `STM_LOG_LEVEL_DEBUG` | 调试 |
| 2 | `STM_LOG_LEVEL_INFO` | 信息（推荐生产环境） |
| 3 | `STM_LOG_LEVEL_WARN` | 警告 |
| 4 | `STM_LOG_LEVEL_ERROR` | 错误 |
| 5 | `STM_LOG_LEVEL_FATAL` | 致命 |
| 6 | `STM_LOG_LEVEL_NONE` | 不输出 |

---

## 4. 会话管理

### 4.1 创建会话（`stm_open_session_create`）

```c
stm_open_session_t* stm_open_session_create(stm_open_session_config_t *config);
```

**配置结构体 `stm_open_session_config_t`：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `client_type` | `stm_client_type_e` | 客户端类型 |
| `session_token` | `char *` | 会话令牌（不能为 NULL） |
| `session_id` | `char *` | 会话 ID（不能为 NULL） |
| `encrypt_key` | `char *` | 加密密钥（不能为 NULL） |
| `on_state` | callback | 状态变化回调（可为 NULL） |
| `on_data_recv` | callback | 数据接收回调（不能为 NULL） |
| `app_data` | `char *` | 应用自定义数据（可为 NULL） |
| `user_data` | `void *` | 透传到回调的用户指针 |

**返回值：** 成功返回 session 句柄，失败返回 NULL。

---

### 4.2 发送数据（`stm_open_session_send`）

```c
stm_ret stm_open_session_send(stm_open_session_t *session, stm_open_data_t *data, int8_t fin);
```

**`stm_open_data_t` 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `event_id` | `char *` | 事件首包必填 |
| `data_type` | `stm_data_type_e` | 数据类型 |
| union | params | 首包按类型填写 `audio_params` / `image_params` 等 |
| `payload` | `uint8_t *` | 数据内容 |
| `payload_length` | `uint32_t` | 数据长度 |

---

### 4.3 关闭会话（`stm_open_session_close`）

```c
void stm_open_session_close(stm_open_session_t *session);
```

---

## 5. 数据类型

| 值 | 宏 | 说明 |
|----|-----|------|
| 1 | `STM_DATA_TYPE_CMD` | 系统指令（如打断） |
| 2 | `STM_DATA_TYPE_VIDEO` | 视频 |
| 3 | `STM_DATA_TYPE_AUDIO` | 音频 |
| 4 | `STM_DATA_TYPE_IMAGE` | 图像 |
| 5 | `STM_DATA_TYPE_FILE` | 文件 |
| 6 | `STM_DATA_TYPE_TEXT` | 文本 |

### 音频参数（`stm_audio_params_t`）

| 字段 | 类型 | 说明 |
|------|------|------|
| `codec_type` | `uint16_t` | 编码类型（101=PCM, 111=OPUS） |
| `sample_rate` | `uint32_t` | 采样率 Hz |
| `channels` | `uint16_t` | 声道数 |
| `bit_depth` | `uint16_t` | 位深度 |
| `frame_duration` | `uint16_t` | 帧时长 ms |
| `frame_size` | `uint16_t` | 帧大小 bytes |

### 图像参数（`stm_image_params_t`）

| 字段 | 类型 | 说明 |
|------|------|------|
| `payload_type` | `uint8_t` | 0=raw, 1=base64, 2=url |
| `format` | `uint8_t` | 1=JPEG, 2=PNG |
| `width` | `uint16_t` | 宽度 |
| `height` | `uint16_t` | 高度 |

---

## 6. 错误码

| 值 | 宏 | 说明 | 处理建议 |
|----|-----|------|----------|
| 0 | `STM_OK` | 成功 | — |
| -1 | `STM_ENOT_INIT` | 未初始化 | 先调用 `stm_open_init` |
| -4 | `STM_EINVALID_PARM` | 无效参数 | 检查指针和字段 |
| -5 | `STM_ENOT_CONNECTED` | 未连接 | 等待 on_state 就绪 |
| -6 | `STM_ECONNECTION_CLOSED` | 连接已关闭 | 重新获取 token 建立会话 |
| -9 | `STM_EAUTH_FAILED` | 鉴权失败 | 检查 token/密钥 |
| -15 | `STM_ETOKEN_EXPIRED` | Token 过期 | 重新获取 session_token |
| -21 | `STM_EBUFFER_NOT_ENOUGH` | 缓冲不足 | 减小单包（建议不超过 200KB） |
| -29 | `STM_ESSL_HANDSHAKE_FAILED` | TLS 握手失败 | 检查时间/证书/网络 |

完整错误码列表见头文件 `stm_errno.h`。

---

## 7. 快速示例

### 文本聊天

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

### 音频聊天

```c
// 首包：带音频参数
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

// 中间包
stm_open_data_t mid = {0};
mid.data_type      = STM_DATA_TYPE_AUDIO;
mid.payload        = pcm_chunk2;
mid.payload_length = chunk_len;
stm_open_session_send(sess, &mid, 0);

// 末包
stm_open_data_t last = {0};
last.data_type      = STM_DATA_TYPE_AUDIO;
last.payload        = pcm_chunk3;
last.payload_length = chunk_len;
stm_open_session_send(sess, &last, 1);  // fin=1
```

---

## 8. 与 RTC TCP Client 的对比

| | RTC Client (`stm_open_*`) | RTC TCP Client (`tai_*`) |
|---|---|---|
| 集成方式 | 预编译静态库 | 源码（PAL 可移植） |
| 协议 | tRTC(tuya自研RTC协议)，UDP 实现 | tRTC(tuya自研RTC协议)，TCP 实现 |
| 会话管理 | 需 session_token（从 IoT Client 获取） | 使用 device_id + local_key 直接连接 |
| API 风格 | 通用 send/recv + data 结构体 | 类型化 API（`send_text`、`send_audio_*`） |
| MCP 支持 | 通过 CMD 类型 | 原生 `TAI_EVT_MCP_CMD` + `tai_send_mcp_response` |
| 平台支持 | macOS/Linux/MIPS/ARM（预编译） | 任何实现了 PAL 的平台 |
| 适合场景 | 已有平台的快速集成 | 新项目、需要源码控制、ESP-IDF |
