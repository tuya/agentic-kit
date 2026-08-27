---
title: VAD 与处理打断
sidebar_label: VAD 与打断
sidebar_position: 2
---

# VAD 与处理打断

本指南涵盖两个密切相关的主题：VAD（语音活动检测）的使用方式，以及如何处理聊天打断事件。

## 两种工作模式

设备端语音交互有两种 VAD 模式，两者的 SDK 调用契约完全不同：

- **云端 VAD（Server VAD，连续对话模式）**：设备持续上行音频，云端检测用户停止说话后主动通知设备，并由云端划分对话回合。
- **设备端 VAD / 手动按键模式**：设备本地（或按键）判定一句话的开始与结束，每句话单独开启/结束一段上行音频流。

:::warning 最常见的错误
在云端 VAD 模式下，收到回合结束信号后调用 `tai_send_audio_end()` 是**错误**的——它会主动结束当前上行 Event，导致云端截断用户语音、连续对话中断。云端 VAD 模式下整个聊天会话只调用一次 `tai_send_audio_start()`，且**不能**调用 `tai_send_audio_end()`。
:::

:::warning 回合结束信号是 `TAI_EVT_CHAT_BREAK`，不是 `TAI_EVT_SERVER_VAD`
当前云端（AI 基础平台）在云端 VAD 模式下**只下发 `TAI_EVT_CHAT_BREAK`（type=4）**，不再下发 `TAI_EVT_SERVER_VAD`（type=5，协议常量仍保留以兼容旧服务端）。设备端应以收到 `TAI_EVT_CHAT_BREAK` 作为回合边界：清除本轮下行 TTS 播放、更新本地"聆听/播报"状态；上行音频流保持打开，等待下一回合。不要按旧文档在 `TAI_EVT_SERVER_VAD` 分支里写业务逻辑——它不会触发。
:::

## 云端 VAD vs 设备端 VAD：SDK 调用对比

| 对比项 | 云端 VAD（Server VAD，连续对话） | 设备端 VAD / 手动按键模式 |
|--------|--------------------------------|--------------------------|
| `tai_send_audio_start()` | 整个聊天会话只调用 **1 次**（会话开始时） | 每句话（每个回合）调用 1 次 |
| `tai_send_audio_chunk()` | 持续发送，包括静默段 | 仅在本地判定为语音时发送（或按键按住期间） |
| `tai_send_audio_end()` | **不能调用** | 每句话结束时调用（本地 VAD 判停 / 按钮松开），通知云端"输入完毕，开始处理" |
| 回合边界判定 | 云端按静音阈值判定（默认约 700 ms，建议配置 600–800 ms） | 设备端本地 VAD 或按钮判定 |
| 回合边界事件 | `TAI_EVT_CHAT_BREAK`（云端检测到用户说完/插话；**云端不再下发 `TAI_EVT_SERVER_VAD`**） | 通常不依赖云端事件（本地已自行判停） |
| 收到 `TAI_EVT_CHAT_BREAK` | 只清除被打断回合的下行 TTS 缓存/播放队列；**不停止上行、不调 `tai_send_audio_end`、不调 `tai_send_audio_start`**；无对应下行缓存则记录日志并忽略 | 同上清下行；之后按本地策略重新 `tai_send_audio_start()` 开启新流 |
| `tai_chat_break()`（设备主动打断） | 用户按键打断 AI 回复时调用；幂等；调用后上行流保持打开 | 同左；调用后按需重新 start 新流 |
| 典型适用场景 | WiFi 音箱、持续供电设备、高交互连续对话 | 电池供电、带宽受限（2G/NB-IoT）设备、按键对讲 |

## 云端 VAD

### 工作原理

Tuya AI 平台提供云端 VAD 能力——设备**在整个会话期间持续发送音频**，云端检测回合边界并在用户停止说话后主动通知设备：

```
会话开始
    │
    v
tai_send_audio_start()（整个会话仅此一次）
    │
    v
持续 tai_send_audio_chunk() ══════════════════════════> 云端 ASR + VAD
    │                                                      │
    │          检测到本回合结束（静音超过阈值）                │
    │  收到 TAI_EVT_CHAT_BREAK <───────────────────────────┘
    v
仅更新本地状态（清除本轮 TTS 播放、结束"聆听中"提示）——上行音频流保持打开
    │
    v
等待 on_text / on_audio 回调（AI 响应播放）
    │
    v
用户再次说话 → 直接复用同一条打开的上行流，进入下一回合
```

**回合（turn）的定义**：从用户开始说话，到 AI 开始回复为止。用户说话中短于静音阈值的停顿不会切分回合。

### 处理回合结束事件

**RTC TCP Client：**

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        // 回合边界（云端不再下发 TAI_EVT_SERVER_VAD，见上文 warning）
        // 1. 停止/清除本轮 TTS 播放（用户可能已插话，本轮回复作废）
        // 2. 更新本地状态：结束"聆听中"提示、切换 UI 等
        stop_playback_and_flush();
        stop_listening_indication();
        // 不要调用 tai_send_audio_end() —— 上行流必须保持打开
        // 也不要调用 tai_send_audio_start() —— 云端 VAD 模式下无需重开
    }
}
```

:::warning
云端 VAD 模式下，收到回合结束信号（当前为 `TAI_EVT_CHAT_BREAK`）**不要**调用 `tai_send_audio_end()`。该函数会结束上行 Event 并通知云端"输入完毕"，在连续对话中调用它会导致云端截断用户语音。`TAI_EVT_SERVER_VAD` 是旧服务端的回合结束信号，当前云端不再下发，仅为协议兼容保留。
:::

### 何时使用 `tai_send_audio_end`

`tai_send_audio_end()` 并非云端 VAD 模式的接口，它只属于设备端 VAD / 手动按键模式：

| 场景 | 是否调用 `tai_send_audio_end()` |
|------|--------------------------------|
| 云端 VAD 连续对话（收到 `TAI_EVT_CHAT_BREAK`） | **否**，上行流保持打开 |
| 手动按键对讲（按钮松开/再次按下停止） | 是，每句结束时调用 |
| 设备端本地 VAD 判停（自带 VAD 算法） | 是，本地判定说完时调用 |
| 用户主动结束会话 | 直接 `tai_disconnect()`，无需先 end |

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
| `asr.enableVad` | bool | 是否启用云端 VAD |
| `processing.interrupt` | bool | 是否启用打断处理 |

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

**接收服务端打断（`TAI_EVT_CHAT_BREAK`，type=4）：**

`TAI_EVT_CHAT_BREAK` 有双重身份：用户在 AI 回复中插话时它是打断信号；在云端 VAD 模式下它同时也是**回合结束信号**（云端检测到用户停止说话后下发，当前云端不再下发 `TAI_EVT_SERVER_VAD`）。两种情况下的设备处理相同：

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        // 1. 停止 TTS 播放
        audio_player_stop();
        // 2. 清空播放缓冲区（丢弃本轮在途 TTS，直到下一个 TAI_STREAM_START）
        audio_buffer_flush();
        // 3. 忽略本轮后续回调
        set_ignore_current_response(true);
        // 不要停止麦克风采音，不要调用 tai_send_audio_end()，
        // 也不要调用 tai_send_audio_start() 重开上行流——
        // 云端 VAD 模式下上行 Event 一直保持打开。
        // 若该打断的 eventId 没有对应的本地下行缓存，记录日志并忽略即可。
    }
}
```

**发送客户端打断：**

```c
// 用户按下按钮打断 AI 回复
tai_chat_break(ctx);   // 幂等，可安全多次调用

// 云端 VAD 模式：到此为止——上行音频流仍保持打开，
// 用户接着说话即可，无需任何 restart。
// 只有设备端 VAD/手动模式才需要为下一句话重新开启音频流：
// tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000);
```

---

## 典型交互流程

### 云端 VAD 连续对话模式

```
会话建立（tai_connect 成功）
    │
    v
tai_send_audio_start()  ←── 整个会话只调用这一次
    │
    v
持续 tai_send_audio_chunk() ═════════════════> 云端 ASR + VAD
    │                                               │
    │    收到 TAI_EVT_CHAT_BREAK（每回合一次）<──────┘
    v
清除本轮 TTS 播放、更新本地状态（上行流不动）
    │
    v
on_text / on_audio 回调 → 播放 AI 响应
    │
    │   播放中用户再次说话（打断，同样收到 CHAT_BREAK）
    v
收到 TAI_EVT_CHAT_BREAK → 停止播放、清空下行缓冲 →（上行流保持打开）
    │
    v
下一回合直接复用同一条上行流 …
    │
    v
一段时间无交互（如 1 分钟）→ tai_disconnect() 进入待机/唤醒词状态
```

### 手动按键 / 设备端 VAD 模式

```
用户按下按钮 / 本地 VAD 检测到语音
    │
    v
tai_send_audio_start()  ←── 每句话调用一次
    │
    v
按住期间持续 tai_send_audio_chunk() ──> 云端 ASR
    │
    v
按钮松开 / 本地判停 → tai_send_audio_end()（通知云端开始处理）
    │
    v
等待 on_text / on_audio 回调（AI 响应播放中）
    │
    │   用户再次按下按钮（打断）
    v
tai_chat_break(ctx) → 停止播放 → 重新 tai_send_audio_start() 开启新流
```

## 注意事项

- 云端 VAD 模式下回合结束信号是 `TAI_EVT_CHAT_BREAK`：只清除本轮下行播放，**不要**调用 `tai_send_audio_end()`（整个会话都不调用），也不要调用 `tai_send_audio_start()` 重开上行流
- `TAI_EVT_SERVER_VAD` 是旧服务端的回合结束信号，当前云端不再下发；协议常量仅为兼容保留，不要在新代码中依赖它
- 手动按键模式下用户松开按钮停止录制时，直接 `tai_send_audio_end()` 即可，无需等待 VAD
