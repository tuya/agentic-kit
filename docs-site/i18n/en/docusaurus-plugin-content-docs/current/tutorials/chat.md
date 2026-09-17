---
title: Voice and text chat
sidebar_label: Voice and text chat
sidebar_position: 2
---

# Voice and text chat

> Corresponding examples:
>      * `examples/posix/ai/rtc-client/`
>      * `examples/posix/ai/rtc-tcp-client/`

:::tip
This chapter uses the precompiled rtc-client library (`stm_open_*` API). The rtc-tcp-client implementation follows similar logic; see the corresponding source code under `examples/`.
:::

This chapter introduces the voice chat example and its implementation. It demonstrates how to use Agentic-kit for voice or text chat with AI and save the returned TTS audio to a local file.

:::note Prerequisites
- Device credentials (`devid`, `secret_key`, `local_key`) -- the example includes default test credentials and can run directly. To use your own device, first complete [Provisioning](./scan-by-device) to obtain credentials.
:::

## Features {#功能概述}

The voice chat example supports two modes:

- **Voice chat mode**: Reads a local 16 kHz, mono, 16-bit PCM audio file and uploads it in 120 ms chunks. The AI performs automatic speech recognition (ASR) and returns a text response and TTS audio.
- **Text-only mode**: If no PCM file is provided, the example sends a preset text greeting. The AI returns a text response and TTS audio.

The example requires the following PCM input parameters:

| Parameter | Value |
|------|---|
| Sample rate | 16000 Hz |
| Channels | 1 (mono) |
| Bit depth | 16-bit |
| Format | Raw PCM (no file header) |
| Frame duration | 120 ms |
| Frame size | 3840 bytes |

**Note**: TuyaAI cloud supports PCM/OPUS audio with different sample rates and other parameters. The example uses fixed PCM parameters for simplicity; these are not mandatory format requirements. See the [Audio format configuration guide](../guides/audio-format).

Both modes:
1. Print the AI's text response to the console in real time.
2. Save the returned TTS audio to `output_chat.pcm`.
3. Collect and print latency measurements, such as the time from the start of sending to the first text packet, and from the end of sending to the first audio packet.

## Run {#运行方式}

```sh
# Text-only mode (no PCM file required)
./build/udp_chat_demo

# Voice mode (provide a PCM file)
./build/udp_chat_demo input.pcm
```

## Key implementation details {#关键实现}

### Initialization and connection {#初始化与连接}

```c
// Initialize the SDK
stm_open_config_t config = { .on_log = log_callback };
stm_open_init(&config);

// Create a Session (token comes from iot_client_get_session_token)
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

### Send audio in chunks {#音频分包发送}

```c
int chunk_size = 3840;   // Number of bytes in 120 ms

// Send chunks in a loop
while (offset < pcm_len) {
    d.event_id = (offset == 0) ? event_id : NULL;  // Only the first chunk carries event_id
    d.payload  = pcm + offset;
    int8_t is_last = (offset + chunk >= pcm_len) ? 1 : 0;
    stm_open_session_send(session, &d, is_last);    // fin=1 for the last chunk
    offset += chunk;
}
```

**Key points:**
- Set `event_id` only for the first chunk; use `NULL` for subsequent chunks.
- Set the last chunk's `fin` flag to `1` to notify the server that sending is complete.
- `codec_type = 101` means raw PCM format.

### Send text {#文本发送}

In text-only mode, send a text message directly:

```c
stm_open_data_t d = {0};
d.data_type      = STM_DATA_TYPE_TEXT;
d.event_id       = event_id;
d.payload        = (uint8_t *)text;
d.payload_length = strlen(text);
stm_open_session_send(session, &d, 1);  // fin=1: send everything at once
```

## Considerations {#注意事项}

- Obtain device credentials through Provisioning. The default credentials in the example are for testing only.
- The PCM file must contain raw PCM data without a file header. Container formats such as WAV are not supported.
- The example waits up to 60 seconds for an AI response, then exits on timeout.
- The connection establishment timeout is 5 seconds.
