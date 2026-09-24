---
title: VAD and Handling Interruptions
sidebar_label: VAD and Interruptions
sidebar_position: 2
---

# VAD and Handling Interruptions

This guide covers two closely related topics: using VAD (voice activity detection) and handling chat break events.

## Two Operating Modes {#两种工作模式}

Device-side voice interaction supports two VAD modes, with completely different SDK call contracts:

- **Server VAD (continuous conversation mode)**: The device continuously sends uplink audio. The cloud detects when the user stops speaking, notifies the device, and determines conversation turn boundaries.
- **Device-side VAD / manual button mode**: The device determines the start and end of each utterance locally (or through a button). Each utterance opens and closes its own uplink audio stream.

:::warning The Most Common Mistake
In server-VAD mode, calling `tai_send_audio_end()` after receiving a turn-end signal is **incorrect**: it actively ends the current uplink Event, causing the cloud to cut off the user's speech and interrupt continuous conversation. In server-VAD mode, call `tai_send_audio_start()` only once for the entire chat Session, and **never** call `tai_send_audio_end()`.
:::

:::warning The Turn-End Signal Is `TAI_EVT_CHAT_BREAK`, Not `TAI_EVT_SERVER_VAD`
The current cloud (Tuya AI Foundation) **only sends `TAI_EVT_CHAT_BREAK` (type=4)** in server-VAD mode. It no longer sends `TAI_EVT_SERVER_VAD` (type=5; the protocol constant remains for compatibility with older servers). Treat an incoming `TAI_EVT_CHAT_BREAK` as a turn boundary: clear this turn's downlink TTS playback and update the local "listening/playing" state. Keep the uplink audio stream open for the next turn. Do not follow older documentation by putting business logic in a `TAI_EVT_SERVER_VAD` branch: it will not fire.
:::

## Server VAD vs Device-Side VAD: SDK Calls {#云端-vad-vs-设备端-vadsdk-调用对比}

| Item | Server VAD (continuous conversation) | Device-side VAD / manual button mode |
|--------|--------------------------------|--------------------------|
| `tai_send_audio_start()` | Call **once** for the entire chat Session, at its start | Call once per utterance (per turn) |
| `tai_send_audio_chunk()` | Send continuously, including silence | Send only while local detection indicates speech (or while the button is held) |
| `tai_send_audio_end()` | **Never call** | Call at the end of each utterance (local VAD detects the end, or the button is released), telling the cloud "input complete; start processing" |
| Turn boundary detection | The cloud uses a silence threshold (about 700 ms by default; 600-800 ms recommended) | Local device VAD or button input |
| Turn boundary event | `TAI_EVT_CHAT_BREAK` (the cloud detects that the user has finished speaking or interrupted; **the cloud no longer sends `TAI_EVT_SERVER_VAD`**) | Usually does not depend on cloud events, because the end has already been detected locally |
| On receiving `TAI_EVT_CHAT_BREAK` | Only clear the interrupted turn's downlink TTS buffer/playback queue; **do not stop the uplink, call `tai_send_audio_end`, or call `tai_send_audio_start`**; if there is no corresponding downlink buffer, log and ignore it | Clear the downlink as on the left; then use `tai_send_audio_start()` to open a new stream according to local policy |
| `tai_chat_break()` (device-initiated chat break) | Call when the user presses a button to interrupt the AI response; idempotent; the uplink stream remains open afterward | Same as on the left; start a new stream afterward as needed |
| Typical scenarios | WiFi speakers, continuously powered devices, highly interactive continuous conversation | Battery-powered devices, bandwidth-constrained devices (2G/NB-IoT), push-to-talk |

## Server VAD {#云端-vad}

### How It Works {#工作原理}

The Tuya AI platform provides server VAD: the device **continuously sends audio throughout the Session**, and the cloud detects turn boundaries and notifies the device when the user stops speaking:

```
Session starts
    |
    v
tai_send_audio_start() (only once for the entire Session)
    |
    v
Continuous tai_send_audio_chunk() ====================> Cloud ASR + VAD
    |                                                       |
    |          Turn end detected (silence exceeds threshold) |
    |  Receive TAI_EVT_CHAT_BREAK <--------------------------+
    v
Update local state only (clear this turn's TTS playback and end the
"listening" indication) -- keep the uplink audio stream open
    |
    v
Wait for on_text / on_audio callbacks (play the AI response)
    |
    v
User speaks again -> reuse the same open uplink stream for the next turn
```

**Definition of a turn**: The period from when the user starts speaking until the AI starts responding. Pauses shorter than the silence threshold do not split an utterance into separate turns.

### Handle the Turn-End Event {#处理回合结束事件}

**RTC TCP Client:**

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        // Turn boundary (the cloud no longer sends TAI_EVT_SERVER_VAD; see warning above)
        // 1. Stop/clear this turn's TTS playback (the user may have interrupted,
        //    invalidating the current response)
        // 2. Update local state: end the "listening" indication, switch UI state, etc.
        stop_playback_and_flush();
        stop_listening_indication();
        // Do not call tai_send_audio_end() -- the uplink stream must remain open
        // Do not call tai_send_audio_start() either -- no restart is needed with server VAD
    }
}
```

:::warning
In server-VAD mode, **do not** call `tai_send_audio_end()` on receiving a turn-end signal (currently `TAI_EVT_CHAT_BREAK`). That function ends the uplink Event and tells the cloud "input complete". Calling it during continuous conversation causes the cloud to cut off the user's speech. `TAI_EVT_SERVER_VAD` is the turn-end signal used by older servers; the current cloud no longer sends it, and it remains only for protocol compatibility.
:::

### When to Use `tai_send_audio_end` {#何时使用-tai_send_audio_end}

`tai_send_audio_end()` is not an interface for server-VAD mode. It belongs only to device-side VAD / manual button mode:

| Scenario | Call `tai_send_audio_end()`? |
|------|--------------------------------|
| Continuous conversation with server VAD (receiving `TAI_EVT_CHAT_BREAK`) | **No**; keep the uplink stream open |
| Manual push-to-talk (button released, or pressed again to stop) | Yes, at the end of each utterance |
| Local device VAD detects the end of speech (using its own VAD algorithm) | Yes, when the device determines that speech has ended |
| User explicitly ends the Session | Call `tai_disconnect()` directly; no prior end call is needed |

### Enable and Configure Server VAD {#启用配置云端-vad}

Pass `chatAttributes` through `event_user_data_json`. This string is sent to the cloud with every EventStart packet. Server VAD is enabled by default in the SDK (leaving this unset uses the built-in defaults). Override it if you need custom settings:

```c
tai_config_t cfg = {
    // ...
    .event_user_data_json =
        "{\"sys.workflow\":\"asr-llm-tts\","
        "\"asr.enableVad\":true,"
        "\"processing.interrupt\":true}",
};
```

| Field | Type | Description |
|------|------|------|
| `asr.enableVad` | bool | Whether to enable server VAD |
| `processing.interrupt` | bool | Whether to enable interruption handling |

### Is Device-Side VAD Necessary? {#设备端-vad-是否需要}

| Scenario | Recommendation |
|------|------|
| WiFi speakers, continuously powered devices | Server VAD alone is sufficient |
| Battery-powered devices | Device-side VAD avoids continuously transmitting silent audio |
| Limited bandwidth (2G/NB-IoT) | Device-side VAD reduces uplink data volume |
| Highly interactive experience required | Hybrid: coarse detection on the device, refined detection in the cloud |

---

## Handle Chat Breaks {#处理聊天打断}

When the user speaks again while the AI is responding, the current response needs to be interrupted.

### Two Directions of Interruption {#打断的两种方向}

| Direction | Initiator | Event | Description |
|------|--------|------|------|
| Server-initiated chat break | The cloud detects the user speaking | `TAI_EVT_CHAT_BREAK` | The device should stop playback |
| Client-initiated chat break | The device actively notifies the cloud | `tai_chat_break()` | Tell the cloud to stop generating the response |

### RTC TCP Client {#rtc-tcp-client}

:::note Independent MQTT control path
A device can keep the IoT MQTT and RTC TCP Connection active together. After `iot_ai_ctrl_set_callback()` is registered, a protocol-9000 `asrInterrupt` is independent of RTC TCP receive backpressure. One application thread should own `iot_client_process()`, publish, and reconnect, while the MQTT callback and `TAI_EVT_CHAT_BREAK` feed the same thread-safe playback policy.

An interruption is decided by **server time**, not by event ID: MQTT `asrInterrupt` carries `data.time`, and TCP `TAI_EVT_CHAT_BREAK` carries `breakAttributes.time` inside attr 111 (not in the event payload). Keep the greatest value as the cutoff, flush the playback queue, then release RTC receive pressure so the worker can keep authenticating and draining old media -- never discard raw TCP bytes, which would corrupt Frame boundaries. In `on_audio`, latch START's `msg->timestamp_ms` and drop every stream at or before the cutoff. A server notice does not require `tai_chat_break()`, and it must not end or reopen a Server-VAD uplink.
:::

**Receive a server-initiated chat break (`TAI_EVT_CHAT_BREAK`, type=4):**

`TAI_EVT_CHAT_BREAK` has two roles: it signals an interruption when the user speaks during the AI response; in server-VAD mode, it is also the **turn-end signal** (sent when the cloud detects that the user has stopped speaking; the current cloud no longer sends `TAI_EVT_SERVER_VAD`). Device-side handling is the same in both cases:

```c
void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    if (msg->event_type == TAI_EVT_CHAT_BREAK) {
        // 1. Stop TTS playback
        audio_player_stop();
        // 2. Read the server interruption time from attr 111 and advance the cutoff
        uint64_t cutoff = parse_break_time(msg->user_data, msg->user_data_len);
        if (cutoff) audio_cutoff_ms = cutoff > audio_cutoff_ms ? cutoff : audio_cutoff_ms;
        // 3. Clear the playback buffer; only streams newer than the cutoff requeue
        audio_buffer_flush();
        // Do not stop microphone capture or call tai_send_audio_end().
        // Do not call tai_send_audio_start() to reopen the uplink stream either:
        // in server-VAD mode, the uplink Event remains open throughout.
        // If attr 111 is missing or its time is invalid, fail closed: treat the
        // in-flight stream's START as the cutoff instead of letting it play on.
    }
}
```

`on_audio` latches START's server time and filters on it:

```c
void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    if (msg->stream_flag == TAI_STREAM_START ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT)
        stream_start_ms = msg->timestamp_ms;   // server time, not local time
    // MIDDLE/END carry no new time; they reuse this turn's latched start
    if (msg->len && stream_start_ms > audio_cutoff_ms)
        audio_buffer_push(msg->data, msg->len);
}
```

**Send a client-initiated chat break:**

```c
// The user presses a button to interrupt the AI response
tai_chat_break(ctx);   // Idempotent; safe to call multiple times

// Server-VAD mode: nothing more to do -- the uplink audio stream remains open.
// The user can simply continue speaking; no restart is needed.
// Only device-side VAD/manual mode needs to reopen the audio stream for the next utterance:
// tai_send_audio_start(ctx, TAI_AUDIO_PCM, 1, 16, 16000);
```

---

## Typical Interaction Flows {#典型交互流程}

### Continuous Conversation with Server VAD {#云端-vad-连续对话模式}

```
Session established (tai_connect succeeds)
    |
    v
tai_send_audio_start()  <-- Call only once for the entire Session
    |
    v
Continuous tai_send_audio_chunk() ================> Cloud ASR + VAD
    |                                                   |
    |    Receive TAI_EVT_CHAT_BREAK (once per turn) <-----+
    v
Clear this turn's TTS playback and update local state (leave uplink unchanged)
    |
    v
on_text / on_audio callbacks -> play the AI response
    |
    |   User speaks during playback (interruption, also receives CHAT_BREAK)
    v
Receive TAI_EVT_CHAT_BREAK -> stop playback, clear downlink buffer
    |                        (keep the uplink stream open)
    v
Reuse the same uplink stream for the next turn...
    |
    v
No interaction for a while (e.g. 1 minute) -> tai_disconnect()
                                           Enter standby/wake-word mode
```

### Manual Button / Device-Side VAD Mode {#手动按键--设备端-vad-模式}

```
User presses the button / local VAD detects speech
    |
    v
tai_send_audio_start()  <-- Call once per utterance
    |
    v
Continuous tai_send_audio_chunk() while the button is held --> Cloud ASR
    |
    v
Button released / local end detected -> tai_send_audio_end()
    |                                  (tell the cloud to start processing)
    v
Wait for on_text / on_audio callbacks (AI response playing)
    |
    |   User presses the button again (interruption)
    v
tai_chat_break(ctx) -> stop playback -> tai_send_audio_start() to open a new stream
```

## Considerations {#注意事项}

- In server-VAD mode, the turn-end signal is `TAI_EVT_CHAT_BREAK`: only clear this turn's downlink playback. **Do not** call `tai_send_audio_end()` at any point during the Session, and do not call `tai_send_audio_start()` to reopen the uplink stream.
- `TAI_EVT_SERVER_VAD` is the turn-end signal used by older servers. The current cloud no longer sends it; the protocol constant remains only for compatibility. Do not rely on it in new code.
- In manual button mode, when the user releases the button to stop recording, call `tai_send_audio_end()` directly. There is no need to wait for VAD.
