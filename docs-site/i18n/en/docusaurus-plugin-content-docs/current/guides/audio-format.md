---
title: Configure Audio Formats
sidebar_label: Configure Audio Formats
sidebar_position: 1
---

# Configure Audio Formats

This guide explains how to select and configure codecs and parameters for uplink and downlink audio.

## Supported Codecs {#支持的编码格式}

| Codec | RTC TCP Client constant | RTC Client codec_type | Description |
|------|--------------------|-----------------------|------|
| PCM | `TAI_AUDIO_PCM` (101) | 101 | Raw, uncompressed audio; simple to implement, but uses more bandwidth |
| Opus | `TAI_AUDIO_OPUS` (111) | 111 | Compressed audio; uses less bandwidth, but requires an encoder/decoder |

## Recommended Parameters {#推荐参数}

| Parameter | Recommended value | Description |
|------|--------|------|
| Sample rate | 16000 Hz | Standard sample rate for voice scenarios |
| Channels | 1 (mono) | Voice does not require stereo |
| Bit depth | 16-bit | Standard PCM bit depth |
| Audio frame duration | 60-120 ms | Balances latency and efficiency |

## RTC TCP Client Configuration {#rtc-tcp-client-配置}

```c
// Start the audio stream with the specified codec parameters
tai_send_audio_start(ctx,
    TAI_AUDIO_PCM,   // codec: PCM or TAI_AUDIO_OPUS
    1,               // channels: mono
    16,              // bit_depth: 16-bit
    16000            // sample_rate: 16kHz
);

// Send an audio frame
tai_send_audio_chunk(ctx, pcm_data, pcm_len);

// End the audio stream
tai_send_audio_end(ctx);   // End in manual mode; never call during a server-VAD Session
```

> When to call `tai_send_audio_end` depends on the VAD mode (continuous conversation with server VAD versus device-side VAD/manual button control). See [VAD and Interruptions](./vad-and-interrupt).

## RTC Client Configuration {#rtc-client-配置}

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
stm_open_session_send(session, &d, 0);  // fin=0: more frames follow
```

## Choosing PCM or Opus {#pcm-vs-opus-选择}

| | PCM | Opus |
|---|---|---|
| Bandwidth | ~256 kbps (16kHz/16bit/mono) | ~16-32 kbps |
| CPU overhead | None | Encoding/decoding required |
| Latency | No additional latency | Encoding frame latency (typically 20-60ms) |
| Suitable scenarios | Ample bandwidth, limited CPU resources | Limited bandwidth (weak WiFi signal, cellular networks) |
| Implementation complexity | Simple | Requires integration of the Opus library |

## Downlink Audio Format {#下行音频格式}

When establishing a Session, the device tells the cloud its supported downlink TTS audio formats through the `tts.order.supports` field in `session_attrs_json`. The cloud sends audio in a format declared by the device.

**Supported downlink formats:**

| Format | Description |
|------|------|
| PCM | Default format; no decoding required |
| Opus | Compressed format that saves downlink bandwidth; requires an Opus decoder |

**Configuration example (RTC TCP Client):**

The default configuration requests PCM downlink:
```c
// Default session_attrs_json (built into the SDK):
// {"deviceMcp":{"supportCustomMCP":true},
//  "tts.order.supports":[{"format":"pcm",
//  "sampleRate":16000,"bitDepth":"16","channels":1}]}
```

To request Opus downlink, set `session_attrs_json`:
```c
tai_config_t cfg = {0};
cfg.session_attrs_json =
    "{\"deviceMcp\":{\"supportCustomMCP\":true},"
    "\"tts.order.supports\":[{\"format\":\"opus\","
    "\"sampleRate\":16000,\"bitDepth\":\"16\",\"channels\":1}]}";
```

**Receive downlink audio:**

The device must handle audio dynamically according to the callback parameters:

```c
// RTC TCP Client
void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    // msg->codec / msg->sample_rate / msg->frame_duration are returned by the cloud
    // For Opus downlink, msg->data contains an Opus-encoded frame; decode before playback
    // For PCM downlink, msg->data contains raw PCM data that can be played directly
    // msg->len is the number of bytes in this audio frame
    // Note: msg and its internal pointers are valid only during the callback;
    // copy the data yourself if you need to retain it
}
```

```c
// RTC Client
void on_data_recv(stm_open_session_t *session, stm_open_data_t *data,
                  int8_t fin, void *user_data)
{
    if (data->data_type == STM_DATA_TYPE_AUDIO) {
        // data->audio_params in the first packet contains the format information
        // Play the payload of subsequent packets directly
    }
}
```

## Considerations {#注意事项}

- Uplink and downlink audio formats can differ (for example, PCM uplink and Opus downlink).
- `OPUS_APPLICATION_VOIP` mode is recommended when encoding with Opus.
- The device declares the downlink TTS format through `tts.order.supports`; no separate configuration in the cloud agent is required.
