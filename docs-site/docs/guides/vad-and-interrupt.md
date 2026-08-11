---
title: VAD 与处理打断
sidebar_label: VAD 与打断
sidebar_position: 2
---

# VAD 与处理打断

本指南涵盖两个密切相关的主题：VAD（语音端点检测）的使用方式，以及如何处理聊天打断事件。

## 云端 VAD

### 工作原理

Tuya AI 平台提供云端 VAD 能力——设备持续发送音频，云端检测到用户停止说话后主动通知设备。

```
设备持续发送音频 ──────────> 云端
                              │
                              │ 检测到用户停止说话
                              v
设备收到 ServerVAD 事件 <──── TAI_EVT_SERVER_VAD
       │
       v
设备停止发送音频（tai_send_audio_end）
```

### 处理 ServerVAD 事件

**RTC TCP Client：**

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_SERVER_VAD) {
        stop_recording();
        tai_send_audio_end(ctx);  // 允许在回调中调用
    }
}
```

### 启用/配置云端 VAD

通过 `event_user_data_json` 传入 `chatAttributes`，该字符串会随每个 EventStart 包发送给云端。SDK 默认已启用云端 VAD（留空时使用内置默认值），如需自定义可覆盖：

```c
tai_config_t cfg = {
    // ...
    .event_user_data_json =
        "{\"sys.workflow\":\"asr-llm-tts\","
        "\"asr.enableVad\":true,"
        "\"processing.interrupt\":true}",
};
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `asr.enableVad` | bool | 是否启用云端 VAD（默认 true） |
| `processing.interrupt` | bool | 是否启用打断处理（默认 true） |

### 设备端 VAD 是否需要？

| 场景 | 建议 |
|------|------|
| WiFi 音箱、持续供电设备 | 仅云端 VAD 即可 |
| 电池供电设备 | 设备端 VAD 避免持续传输静默音频 |
| 带宽受限（2G/NB-IoT） | 设备端 VAD 减少上行数据量 |
| 高交互体验要求 | 混合：设备端做粗判，云端做精判 |

---

## 处理聊天打断

当用户在 AI 回复过程中再次说话时，需要"打断"当前回复。

### 打断的两种方向

| 方向 | 触发者 | 事件 | 说明 |
|------|--------|------|------|
| 服务端打断 | 云端检测到用户说话 | `TAI_EVT_CHAT_BREAK` | 设备应停止播放 |
| 客户端打断 | 设备端主动通知 | `tai_chat_break()` | 通知云端停止生成 |

### RTC TCP Client

**接收服务端打断：**

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        // 1. 停止 TTS 播放
        audio_player_stop();
        // 2. 清空播放缓冲区
        audio_buffer_flush();
        // 3. 忽略本轮后续回调
        set_ignore_current_response(true);
    }
}
```

**发送客户端打断：**

```c
// 用户按下按钮打断，或设备端 VAD 检测到新一轮说话
tai_chat_break(ctx);

// 然后开始新的音频流
tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000);
```

### RTC Client

```c
void on_data_recv(stm_open_session_t *session, stm_open_data_t *data,
                  int8_t fin, void *user_data)
{
    if (data->data_type == STM_DATA_TYPE_CMD) {
        // 收到系统指令（打断）
        audio_player_stop();
        audio_buffer_flush();
    }
}
```

---

## 典型交互流程

```
用户按下按钮 → 开始录制
    │
    v
tai_send_audio_start()
    │
    v
持续 tai_send_audio_chunk() ──> 云端 ASR + VAD
    │                              │
    │  收到 TAI_EVT_SERVER_VAD <───┘
    v
tai_send_audio_end()
    │
    v
等待 on_text / on_audio 回调（AI 响应播放中）
    │
    │  用户再次说话（打断）
    v
收到 TAI_EVT_CHAT_BREAK → 停止播放 → 开始新一轮
```

## 注意事项

- 收到 `TAI_EVT_SERVER_VAD` 后应尽快调用 `tai_send_audio_end()`
- 收到 `TAI_EVT_CHAT_BREAK` 后，被打断的回复可能不会发送 `fin=1`
- `tai_chat_break()` 是幂等的，多次调用不会出错
- 如果用户通过按钮手动停止录制，直接 `tai_send_audio_end()` 即可，无需等待 VAD
