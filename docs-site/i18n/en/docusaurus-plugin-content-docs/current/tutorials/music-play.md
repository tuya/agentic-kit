---
title: Music playback
sidebar_label: Music playback
sidebar_position: 3
---

# Music playback

> Corresponding example:
>      * `examples/posix/ai/rtc-tcp-client/music_play_demo.c`

:::note Prerequisites
- Device credentials (`devid`, `secret_key`, `local_key`) -- the example includes default test credentials and can run directly. To use your own device, first complete [Provisioning](./scan-by-device) to obtain credentials.
- This example requires `curl` to be installed on the system to download audio preview clips.
:::

This chapter introduces the music playback example and its implementation. It demonstrates how to use Agentic-kit to send a text instruction that triggers the cloud AI's Music Skill, parse the returned song metadata (song title, artist, album, and audio URL), and download a preview clip locally.

## Features {#功能概述}

The music playback example simulates a typical **smart speaker** scenario:

1. The user sends a text instruction, such as "播放周杰伦的歌" ("Play songs by Jay Chou").
2. The AI recognizes the intent, triggers the Music Skill, and returns song information in a SKILL response.
3. The example parses the song metadata and displays it in the console.
4. It downloads an audio preview clip to a local file using `curl`.

The example:
- Prints the AI's NLG text to the console as it streams in.
- Extracts and formats song information when it detects a music SKILL response.
- Downloads a preview clip to `output_music.mp3`.
- Includes application-side reconnection logic with exponential backoff and a circuit breaker.

## Build {#编译}

```sh
cd examples/posix
cmake -S . -B build
cmake --build build --target music_play_demo
```

You can also build all POSIX examples:

```sh
cmake --build build
```

## Run {#运行方式}

Run the following commands from `examples/posix`. The Chinese query strings are literal demo inputs: the default means "Play songs by Jay Chou", and the custom query means "Play pop music".

```sh
# Default query ("播放周杰伦的歌"), using built-in test credentials
./build/music_play_demo

# Custom query
./build/music_play_demo "播放流行音乐"

# All arguments
./build/music_play_demo [query] [devid] [secret_key] [local_key]
```

| Argument | Description | Default |
|------|------|--------|
| `query` | Text instruction | `播放周杰伦的歌` ("Play songs by Jay Chou") |
| `devid` | Device ID | Built-in test device |
| `secret_key` | Device secret key | Built-in test key |
| `local_key` | Local key | Built-in test key |

Example console output after a successful run (literal Chinese output preserved):

```
=== music_play_demo ===
Device ID : 6cd370251e8be96de8vwoe
Query     : 播放周杰伦的歌
[main] Connecting to TAI server...
[main] Connected.

[main] Sending text: "播放周杰伦的歌"
Response: 正在为您播放周杰伦的歌

  +------------------------------------------+
  |               MUSIC FOUND                |
  +------------------------------------------+
  | Song    : 开不了口                       |
  | Artist  : 周杰伦                         |
  | Album   : 范特西                         |
  | Format  : mp3                            |
  | AudioID : ...                            |
  +------------------------------------------+
  Audio : https://...mp3
  Cover : https://...jpg

[main] Downloading: https://...mp3
[main] Saved to: output_music.mp3
[main] Play with: afplay output_music.mp3   (macOS)
                  mpv output_music.mp3      (Linux)

Done.
```

The response means "Playing songs by Jay Chou for you". The song is "开不了口" ("Can't Speak"), by Jay Chou ("周杰伦"), from the album *Fantasy* ("范特西"). These literal metadata values also appear in the SKILL response below.

The exit code can be used directly in scripts: `0` means a conversation completed normally; `1` means the connection timed out, a detected music response failed to parse, or the preview download failed. A query that does not trigger the Music Skill (the AI replies with text only) is not a failure.

## Key implementation details {#关键实现}

This example uses rtc-tcp-client (the open-source TCP transport layer) and its `tai_*` API.

### Overall flow {#整体流程}

```
iot_client_init → iot_client_get_session_token → parse_token
    → tai_ctx_init → tai_connect → tai_send_text → wait for callbacks → tai_disconnect
```

### 1. Obtain a Session token {#1-获取会话-token}

Use iot-client to obtain a Session token containing the TAI Connection information:

```c
iot_client_t *iot = iot_client_init(&iot_cfg);

char *token = calloc(1, 4096);
iot_client_get_session_token(iot, NULL, token, 4096);
```

Base64-decoding the token yields JSON with two parts: the connection address (`connect_conf`) and Session configuration (`session_conf`).

### 2. Initialize the TAI Connection {#2-初始化-tai-连接}

```c
tai_config_t tai_cfg = {
    .host              = cp.host,
    .port              = cp.port,
    .tls_sni           = cp.tls_sni,
    .device_id         = cp.derived_client_id,
    .local_key         = local_key,
    .protocol_version  = TAI_VER_21,
    .client_type       = TAI_CLIENT_DEVICE,
    .sign_level        = TAI_SIGN_HMAC_SHA256,
    .biz_code          = cp.biz_code,
    .biz_tag           = cp.biz_tag,
    .agent_token       = cp.agent_token,
    .session_attrs_json   = SESSION_ATTRS,
    .event_user_data_json = EVENT_USER_DATA,
    .pal               = pal,
    .on_text           = on_text,
    .on_audio          = on_audio,
    .on_event          = on_event,
    .on_disconnect     = on_disconnect,
    .user_data         = &dc,
};

tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
```

### 3. Send a text instruction {#3-发送文本指令}

```c
tai_connect(ctx);
tai_send_text(ctx, query, strlen(query));
```

### 4. Parse the music SKILL response {#4-解析音乐-skill-响应}

After triggering the Music Skill, the AI returns a structured SKILL response through the `on_text` callback. The response format is:

```json
{
  "bizType": "SKILL",
  "data": {
    "code": "music",
    "general": {
      "action": "play",
      "data": {
        "audios": [{
          "name": "开不了口",
          "artist": "周杰伦",
          "album": "范特西",
          "format": "mp3",
          "url": "https://...mp3",
          "audioId": "...",
          "imageUrl": "..."
        }]
      }
    }
  }
}
```

The example's `on_text` callback does two things: it prints each NLG text chunk immediately to preserve streaming output, and it accumulates all chunks to parse the SKILL structure once the stream ends:

```c
static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    /* NLG text: each chunk is a self-contained JSON line. Print it on arrival,
       bounded by msg->len, decoding \n / \" / \uXXXX escapes. A return value
       of 1 means this chunk was NLG and handled, including an empty final chunk
       such as {"content":""}: it is still NLG and must not be printed verbatim. */
    if (nlg_print_content(msg->text, msg->len))
        dc->stream_printed = 1;

    /* Also accumulate the full stream: a SKILL response is one JSON document
       that may span chunks. Parse it only after it has been fully assembled. */
    if (demo_textbuf_accum(&dc->text, msg) == 1)
        handle_complete_text(dc);   /* is_music_response → try_parse_music */
}
```

The server might not send a separate text END chunk (the SDK discards empty text frames). As a fallback, `on_event` calls `demo_textbuf_flush()` when it receives `TAI_EVT_END` (the end of the Event), delivering any buffered stream.

:::caution Two essential constraints
- **`msg->text` is not `\0`-terminated.** `tuya_ai.h` explicitly marks this pointer as borrowed from the SDK receive buffer and not NUL-terminated. Calling `strstr` / `strchr` / `strcmp` on it directly can read beyond `msg->len` into bytes left by the previous packet. All parsing must first copy the data out using `msg->len`.
- **Text is delivered in chunks identified by `stream_flag`** (`TAI_STREAM_START` / `MIDDLE` / `END`, or a single `ONE_SHOT`). Printing can be done chunk by chunk, but parsing JSON requires reassembling the entire stream first; otherwise, `"code":"music"` and `audios` may be in different chunks.

`demo_textbuf_accum()` / `demo_textbuf_flush()` in `demo_text.h` handle both constraints. Before reconnecting, use `demo_textbuf_reset()` to discard any partial stream from the old Connection.
:::

:::info The buffer holds only one stream
`demo_textbuf_t` reassembles only one text stream at a time. It also **cannot** separate two interleaved streams within an Event: `tai_text_msg_t` has no field that can distinguish them. All text packets within the same Event share the same `event_id` (the SDK latches only one Event ID and clears it after `TAI_EVT_END`) and the same `data_id` (`TAI_DATA_ID_TEXT_DOWN`).

What it can do is **detect** two situations:

**A new stream replaces an unfinished one** -- a `START` / `ONE_SHOT` arrives before the previous stream's END. Because the buffer holds only one stream, the old one is inevitably lost. Rather than discard it silently, the example increments `tb->dropped` and logs a warning:

```
[demo_text] a new stream started while 214 bytes of the previous one were still buffered: dropping those — ...
```

**A gap in `seq`** -- `seq` counts text packets within an Event, so a gap indicates that a chunk the application did not see consumed a sequence number. However, this signal is **ambiguous**: the SDK itself discards zero-length text frames (`media_text()` in `tai_protocol.c` delivers data only when `payload_len > off`). These frames contain no bytes, so assembling across them produces the correct document. Continuing accumulation mixes two documents only if the missing chunk belongs to another interleaved stream; that mixture is then rejected during JSON parsing. The default policy is therefore to **log a warning and continue accumulating**:

```
[demo_text] text seq gap (11 -> 13): continuing — ...
```

If interleaving is the more likely cause in a particular deployment, use `-DDEMO_TEXT_SEQ_CHECK=2` to discard the stream on a gap instead. `-DDEMO_TEXT_SEQ_CHECK=0` disables the check entirely.

Either kind of stream loss increments `demo_textbuf_t.dropped` (`demo_textbuf_reset()` does not clear it). The example checks this counter before exiting to determine success or failure: reporting "no music response for this query" and returning 0 after losing a stream would cause scripts to treat data loss as a successful run.
:::

Also, read the `code` field from the SKILL envelope's `data` object, not from the first match in the entire document. With the common outer structure `{"code":0,"msg":"ok","data":{"code":"music",...}}`, taking the first `code` yields status code `0` and silently discards the music response.

## Shared helper headers {#公共辅助头文件}

The five examples under `examples/posix/ai/rtc-tcp-client/` share the following helper headers instead of duplicating parsing code:

| Header | Contents |
|--------|------|
| `demo_json.h` | Minimal JSON reading (string-aware bracket matching and decoding of `\"` / `\/` / `\uXXXX` escapes), Base64 decoding, Session token parsing, and bounded copies into fixed-size configuration fields |
| `demo_text.h` | Safe handling of `tai_text_msg_t`: length-bounded searches, NLG content decoding and printing, text stream reassembly, and tracking of dropped streams |
| `demo_mcp.h` | Device-side MCP responses: echo the request `id` and return the correct shape for each method; devices without tools use `demo_mcp_reply_no_tools()` |
| `demo_reconnect.h` | Application-side reconnection policy (exponential backoff and a circuit breaker) |

Every function in `demo_json.h` requires a **`\0`-terminated** buffer. Copy callback `msg->text` / `msg->data` first.

## NLG text output {#nlg-文本输出}

NLG text outside the music response (the text of the AI's spoken reply) is printed incrementally as it streams in. The example uses `nlg_print_content()` to extract the JSON `content` field and print only its text after **decoding JSON escapes**. The server often encodes Chinese characters as `\uXXXX`; decoding renders readable Chinese characters rather than escape sequences. For example, the literal response below means "Playing songs by Jay Chou for you":

```
Response: 正在为您播放周杰伦的歌
```

## Copyright and licensing {#版权说明}

:::note
The music rights and service capabilities described here are provided by Tuya's content server. Features and billing policies may change with the service provider's policies. For current information, see [Music and Story Skill (Chinese)](https://developer.tuya.com/cn/docs/iot/music_tool?id=Keziqxnjdvn6c).
:::

**Preview limitations**

By default, the AI music feature returns **preview versions** of songs with a duration limit (typically 30 seconds), intended only for feature evaluation and development debugging. Playing complete songs in production products requires purchasing the corresponding advanced music capability license.

**NetEase Cloud Music integration**

Tuya's content server integrates **NetEase Cloud Music** to provide search and playback of licensed music. The integration process is:

1. **Purchase the advanced capability**: In the [Product Development](https://platform.tuya.com/pmg/list) flow, enable **AI NetEase Cloud Music Playback** under **Function Definition > Advanced Product Features**, then pay the per-device license fee through [Deliverable Procurement](https://platform.tuya.com/purchase/index?type=1). The license is valid for three years from device Activation and first use.
2. **Add music tools**: On the agent development page, add **Music and Story Skill** under **Skill Configuration > Tool Set**. It includes tools for media playback control and searching for and playing music or children's songs.
3. **Deploy the agent**: Deploy the agent with the Music Skill to a product (PID) that has purchased the advanced capability. Devices can then play complete songs.

:::important NetEase Cloud Music considerations
- NetEase Cloud Music supports only the **China data center**. Devices in other regions can play previews only.
- The license excludes Black Vinyl VIP membership music. To play membership content, end users must link their NetEase membership account through **Third-Party Content Authorization** in the App's device panel.
- NetEase Cloud Music supports **device-side playback only**, not cloud-only playback.
:::

For detailed platform configuration steps, see the official [Music and Story Skill documentation (Chinese)](https://developer.tuya.com/cn/docs/iot/music_tool?id=Keziqxnjdvn6c).

## Considerations {#注意事项}

- Obtain device credentials through Provisioning. The default credentials are for testing only. The device ID and keys are written to fixed-size fields in `iot_client_config_t` (32 bytes each). The example exits with an error if a value is too long; it does not truncate it.
- Preview downloads depend on the system's `curl`; make sure it is installed. The example invokes `curl` directly with `fork` + `execvp`, **without a shell**. The server-provided URL is passed as a single argv element and cannot be executed as a command. Only URLs starting with `http://` / `https://` are accepted. Retain this constraint when implementing downloads on a device.
- The Music Skill requires a correctly configured workflow on the Tuya AI platform; otherwise, no SKILL response is returned.
- The example waits up to 60 seconds for an AI response, then exits on timeout.
- The current example parses and displays only the first song. To play full audio, implement an audio player on the device.
- The metadata box aligns by **display width** (Chinese characters occupy two columns), not byte count. Overlong fields are truncated at character boundaries.
- The `on_audio` callback is empty in this example and does not process TTS audio.
- This example declares MCP support but implements no tools. When `on_event` receives `TAI_EVT_MCP_CMD`, it responds using `demo_mcp_reply_no_tools()` from `demo_mcp.h`. Note that **the SDK's built-in default attributes already enable MCP**, so devices that omit `session_attrs_json` also receive MCP requests and must respond correctly. See `mcp_demo.c` to implement actual device tools.
