---
title: Frequently Asked Questions (FAQ)
sidebar_label: FAQ
sidebar_position: 100
slug: /faq
---

# Frequently Asked Questions (FAQ)

## How do I request RTC Client library support for a new platform/architecture? {#如何请求-rtc-client-库支持新的平台架构}

The RTC Client (`stm_open_*`) is provided as a precompiled static library. It currently supports:

- macOS arm64
- Linux x86_64 / aarch64
- Linux ARM (Rockchip830)
- MIPS (Ingenic)

**To request a new platform:**

1. Prepare the information: target platform name, toolchain information (`gcc -v` output), target triple, and BSP link
2. Submit the request through Tuya technical support or your project lead
3. A precompiled library for the new platform can usually be delivered within 1-2 weeks

**Alternative:** Use the [RTC TCP Client](./reference/rtc-tcp-client) (source form); you only need to implement the `pal.h` interfaces to run on any platform. See [Porting to a new platform](./guides/porting-to-new-platform) for details.

## Does the device need VAD? {#设备端是否需要-vad}

**Not required.** The cloud provides Server VAD, so the device can simply keep sending audio; the cloud notifies the device when it detects that the user has stopped speaking. Currently the cloud uses `TAI_EVT_CHAT_BREAK` as the end-of-turn signal (`TAI_EVT_SERVER_VAD` is no longer sent; the constant is retained only for protocol compatibility). Note: in cloud VAD mode, after receiving the end-of-turn signal, do **not** call `tai_send_audio_end()`; keep the uplink audio stream open for the whole Session.

**However, device-side VAD is recommended for the following scenarios:**

| Scenario | Recommendation |
|------|------|
| Battery-powered devices | Device-side VAD avoids continuously transmitting silent audio |
| Bandwidth-constrained (e.g. 2G/NB-IoT) | Device-side VAD reduces the volume of uplink data |
| WiFi speakers, continuously powered devices | Cloud VAD alone is sufficient |
| Demanding interactivity requirements | Hybrid: the device does coarse detection and the cloud does fine detection |

See [VAD and interruptions](./guides/vad-and-interrupt) for details.

## What is the difference between RTC TCP Client and RTC Client, and which should I use? {#rtc-tcp-client-和-rtc-client-的区别用哪个}

| | RTC TCP Client (`tai_*`) | RTC Client (`stm_open_*`) |
|---|---|---|
| Integration form | Source code + PAL | Precompiled static library |
| Protocol | tRTC (Tuya's proprietary RTC protocol), TCP implementation | tRTC (Tuya's proprietary RTC protocol), UDP implementation |
| API style | Typed (`send_text`, `send_audio_*`) | Generic data structures |
| Platform extensibility | Implement the PAL | Requires waiting for an official library build for the new platform |

The UDP implementation has excellent weak-network performance and works well even under poor network conditions. If your use case involves weak networks, prefer the rtc-client.

For other scenarios, use whichever you prefer.

## Which audio formats are supported? {#支持哪些音频格式}

**Uplink (device → cloud):**
- PCM: 16 kHz / 16-bit / mono (recommended; simple to implement)
- Opus: 16 kHz / mono (recommended for bandwidth-constrained scenarios)

**Downlink (cloud → device):**
- Determined by the cloud Agent TTS configuration
- Usually PCM or Opus
- Format information is provided in the first audio callback (`sample_rate`, `frame_duration`)

See the [Audio format configuration guide](./guides/audio-format) for details.

## What should I do if session_token expires? {#session_token-过期了怎么办}

**RTC Client:**
- A token is usually valid for 12-24 hours
- After expiry you receive an `STM_ETOKEN_EXPIRED` error or an `on_state` callback
- Call `iot_client_get_session_token()` again to obtain a new token, then create a new Session

**RTC TCP Client:**
- Establishing a connection requires an `agent_token` (obtained via `iot_client_get_session_token()`), but once established it uses a long-lived connection with Ping/Pong keepalive, so the token does not need periodic refreshing
- If the connection drops (the `on_disconnect` callback), the thread that holds the `tai_ctx_t` can simply call `tai_connect()` again

:::caution Do not reconnect inside a callback
All callbacks run on a background worker thread. **Never** call `tai_connect()` / `tai_disconnect()` / `tai_ctx_deinit()` inside a callback (including `on_disconnect`) — these functions join the worker thread and cause a self-deadlock.

The correct approach: in the callback, only set a flag (or call `tai_request_disconnect()`), then, after the callback returns, have the thread that holds the `tai_ctx_t` run `tai_disconnect()` followed by `tai_connect()` to complete the reconnect.
:::

## What kind of network conditions does the device need? {#设备需要什么样的网络条件}

| Metric | Minimum requirement | Recommended |
|------|---------|------|
| Bandwidth (uplink) | 32 kbps (Opus) / 256 kbps (PCM) | 512 kbps+ |
| Bandwidth (downlink) | 64 kbps | 512 kbps+ |
| Latency | < 500 ms RTT | < 200 ms RTT |
| Stability | Occasional packet loss is recoverable | Stable WiFi/Ethernet |

- Using Opus encoding can greatly reduce bandwidth requirements
- Offline mode is not supported — a continuous network connection is required
- Reconnect after a dropped connection is supported (the RTC Client has a Recovering state; the RTC TCP Client requires application-level reconnect)
