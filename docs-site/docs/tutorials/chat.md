---
title: 语音/文本聊天
sidebar_label: 语音/文本聊天
sidebar_position: 2
---

# 语音/文本聊天

> 对应示例：
>      * `examples/posix/ai/rtc-client/`
>      * `examples/posix/ai/rtc-tcp-client/`

:::tip
本章以预编译库版 rtc-client（`stm_open_*` API）为例进行说明。rtc-tcp-client 的实现逻辑类似，请参考 `examples/` 下的对应源码。
:::

本章介绍语音聊天示例的功能与实现。该示例演示了如何通过 Agentic-kit 实现与 AI 的语音或文本聊天，并将 AI 返回的 TTS 音频保存到本地文件。

:::note 前置条件
- 设备凭据（`devid`、`secret_key`、`local_key`）—— 示例内置默认测试凭据，可直接运行。用自己的设备时需先完成[配网](./scan-by-device)获取凭据。
:::

## 功能概述

语音聊天示例支持两种模式：

- **语音聊天模式**：读取本地 16kHz/mono/16-bit 的 PCM 音频文件，按 120ms 帧分
  包上传，AI 会进行语音识别（ASR）并返回文本回复和 TTS 语音。
- **纯文本模式**：不提供 PCM 文件时，示例会发送一段预设的文本问候语，AI
  返回文本回复和 TTS 语音。

示例代码要求的 PCM 输入参数：

| 参数 | 值 |
|------|---|
| 采样率 | 16000 Hz |
| 声道数 | 1（单声道） |
| 位深 | 16-bit |
| 格式 | 原始 PCM（无文件头） |
| 帧时长 | 120 ms |
| 帧大小 | 3840 字节 |


**注**：TuyaAI 云端支持 PCM/OPUS 音频以及不同的采样率及其他参数，示例中为简单起见使用了固定的 PCM 格式参数，并非必需的格式。详见[音频格式配置指南](../guides/audio-format)。

两种模式都会：
1. 在控制台实时打印 AI 返回的文本内容
2. 将 AI 返回的 TTS 音频保存到 `output_chat.pcm`
3. 统计并输出延迟信息（发送开始到首包文字、发送结束到首包音频等）

## 运行方式

```sh
# 纯文本模式（不需要 PCM 文件）
./build/chat_demo

# 语音模式（提供 PCM 文件）
./build/chat_demo input.pcm
```

## 关键实现

### 初始化与连接

```c
// 初始化 SDK
stm_open_config_t config = { .on_log = log_callback };
stm_open_init(&config);

// 创建会话（token 来自 iot_client_get_session_token）
stm_open_session_config_t sess_cfg = {
    .client_type   = STM_CLIENT_TYPE_DEVICE,
    .session_token = token,
    .session_id    = session_id,
    .encrypt_key   = local_key,
    .on_state      = on_state_cb,
    .on_data_recv  = on_data_recv_cb,
};
stm_open_session_t *session = stm_open_session_create(&sess_cfg);
```

### 音频分包发送

```c
int chunk_size = 3840;   // 120ms 对应的字节数

// 循环分包发送
while (offset < pcm_len) {
    d.event_id = (offset == 0) ? event_id : NULL;  // 仅首包携带 event_id
    d.payload  = pcm + offset;
    int8_t is_last = (offset + chunk >= pcm_len) ? 1 : 0;
    stm_open_session_send(session, &d, is_last);    // 末包 fin=1
    offset += chunk;
}
```

**要点：**
- `event_id` 仅在首包设置，后续包置 `NULL`
- 最后一个包的 `fin` 标志设为 `1`，通知服务端数据发送完毕
- `codec_type = 101` 表示原始 PCM 格式

### 文本发送

纯文本模式下直接发送一条文本消息：

```c
stm_open_data_t d = {0};
d.data_type      = STM_DATA_TYPE_TEXT;
d.event_id       = event_id;
d.payload        = (uint8_t *)text;
d.payload_length = strlen(text);
stm_open_session_send(session, &d, 1);  // fin=1, 一次性发送
```

## 注意事项

- 设备凭据需要通过配网流程获取，示例中的默认凭据仅供测试使用。
- PCM 文件必须是无文件头的裸 PCM 数据，不支持 WAV 等容器格式。
- AI 响应的最大等待时间为 60 秒，超时后示例会退出。
- 连接建立的等待时间为 5 秒。

