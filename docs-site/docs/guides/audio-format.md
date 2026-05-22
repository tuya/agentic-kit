---
title: 如何配置音频格式
sidebar_label: 配置音频格式
sidebar_position: 1
---

# 如何配置音频格式

本指南说明如何选择和配置上行/下行音频的编码格式和参数。

## 支持的编码格式

| 编码 | RTC TCP Client 常量 | RTC Client codec_type | 说明 |
|------|--------------------|-----------------------|------|
| PCM | `TAI_AUDIO_PCM` (101) | 101 | 原始无压缩音频，实现简单，带宽占用大 |
| Opus | `TAI_AUDIO_OPUS` (111) | 111 | 压缩编码，带宽占用小，需要编解码器 |

## 推荐参数

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| 采样率 | 16000 Hz | 语音场景的标准采样率 |
| 声道数 | 1（单声道） | 语音不需要立体声 |
| 位深 | 16-bit | PCM 标准位深 |
| 帧时长 | 60-120 ms | 兼顾延迟和效率 |

## RTC TCP Client 配置

```c
// 开始音频流（指定编码参数）
tai_send_audio_start(ctx,
    TAI_AUDIO_PCM,   // codec: PCM 或 TAI_AUDIO_OPUS
    1,               // channels: 单声道
    16,              // bit_depth: 16-bit
    16000            // sample_rate: 16kHz
);

// 发送音频帧
tai_send_audio_chunk(ctx, pcm_data, pcm_len);

// 结束音频流
tai_send_audio_end(ctx);
```

## RTC Client 配置

```c
stm_open_data_t d = {0};
d.event_id   = event_id;
d.data_type  = STM_DATA_TYPE_AUDIO;
d.audio_params = (stm_audio_params_t){
    .codec_type     = 101,    // PCM=101, OPUS=111
    .sample_rate    = 16000,
    .channels       = 1,
    .bit_depth      = 16,
    .frame_duration = 120,    // ms
    .frame_size     = 3840,   // 16000 * 16/8 * 0.12 = 3840 bytes
};
d.payload        = pcm_frame;
d.payload_length = frame_len;
stm_open_session_send(session, &d, 0);  // fin=0, 还有更多帧
```

## PCM vs Opus 选择

| | PCM | Opus |
|---|---|---|
| 带宽 | ~256 kbps (16kHz/16bit/mono) | ~16-32 kbps |
| CPU 开销 | 无 | 需要编解码 |
| 延迟 | 无额外延迟 | 编码帧延迟（通常 20-60ms） |
| 适用场景 | 带宽充足、CPU 受限 | 带宽受限（WiFi 弱信号、移动网络） |
| 实现复杂度 | 简单 | 需集成 Opus 库 |

## 下行音频格式

下行 TTS 音频格式由设备在建立会话时通过 `session_attrs_json` 中的 `tts.order.supports` 字段告知云端，云端会按照设备声明的支持格式下发音频。

**支持的下行格式：**

| 格式 | 说明 |
|------|------|
| PCM | 默认格式，无需解码 |
| Opus | 压缩格式，节省下行带宽，需要 Opus 解码器 |

**配置示例（RTC TCP Client）：**

默认配置请求 PCM 下行：
```c
// 默认 session_attrs_json（内置于 SDK）:
// {"deviceMcp":{"supportCustomMCP":true},
//  "tts.order.supports":[{"format":"pcm",
//  "sampleRate":16000,"bitDepth":"16","channels":1}]}
```

如需请求 Opus 下行，设置 `session_attrs_json`：
```c
tai_config_t cfg = {0};
cfg.session_attrs_json =
    "{\"deviceMcp\":{\"supportCustomMCP\":true},"
    "\"tts.order.supports\":[{\"format\":\"opus\","
    "\"sampleRate\":16000,\"bitDepth\":\"16\",\"channels\":1}]}";
```

**接收下行音频：**

设备端需要根据回调中的参数动态处理：

```c
// RTC TCP Client
void on_audio(tai_ctx_t *ctx, const uint8_t *data, size_t len,
              uint32_t sample_rate, uint16_t frame_duration, void *ud)
{
    // sample_rate 和 frame_duration 由云端返回
    // 如果请求了 Opus 下行，data 为 Opus 编码帧，需解码后播放
    // 如果请求了 PCM 下行，data 为原始 PCM 数据，可直接播放
}
```

```c
// RTC Client
void on_data_recv(stm_open_session_t *session, stm_open_data_t *data,
                  int8_t fin, void *user_data)
{
    if (data->data_type == STM_DATA_TYPE_AUDIO) {
        // 首包的 data->audio_params 包含格式信息
        // 后续包直接播放 payload
    }
}
```

## 注意事项

- 上行和下行音频格式可以不同（例如上行 PCM，下行 Opus）
- Opus 编码时建议使用 `OPUS_APPLICATION_VOIP` 模式
- 下行 TTS 格式由设备端通过 `tts.order.supports` 声明，不需要在云端 Agent 单独配置
