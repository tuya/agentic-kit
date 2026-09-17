---
title: Learning camera (image understanding)
sidebar_label: Image understanding
sidebar_position: 4
---

# Learning camera (image understanding)

> Corresponding examples:
>      * `examples/posix/ai/rtc-client/`
>      * `examples/posix/ai/rtc-tcp-client/`

:::tip
This chapter uses the precompiled rtc-client library (`stm_open_*` API). The rtc-tcp-client implementation follows similar logic; see the corresponding source code under `examples/`.
:::

This chapter introduces the image understanding example and its implementation. It demonstrates how to use Agentic-kit to send an image and a text prompt to AI and receive structured JSON results and a TTS voice response.

:::note Prerequisites
- Device credentials (`devid`, `secret_key`, `local_key`) -- the example includes default test credentials and can run directly. To use your own device, first complete [Provisioning](./scan-by-device) to obtain credentials.
- This example depends on a **Workflow** configured on the Tuya AI platform. See [Create a workflow](../guides/create-workflow).
:::

## Features {#功能概述}

The edu-camera example simulates a typical **learning camera** scenario:

1. The user presses the shutter button and the device takes a photo.
2. The device sends the photo and a trigger instruction (prompt) to the cloud AI.
3. The AI returns structured JSON (card information such as a name, pinyin, and English term) and a TTS voice announcement.

The example:
- Reads a local image file (JPEG or PNG, up to 10 MB).
- Sends the text prompt first (`fin=0`), then the image data (`fin=1`).
- Prints the AI's text/JSON response to the console in real time.
- Saves TTS audio to a local file, automatically determining the output format (PCM/MP3/OGG, etc.) from the first audio packet's parameters.
- Collects and prints latency measurements.

## Run {#运行方式}

```sh
# Run from examples/posix (so the default relative path res/test.jpg resolves)
./build/udp_edu_camera_demo

# When running from any other directory, pass an absolute image path
./build/udp_edu_camera_demo /absolute/path/to/test.jpg "image_recognition"

# All arguments
./build/udp_edu_camera_demo <img_path> <prompt> <audio_path> <devid> <secret_key> <local_key>
```

## Key implementation details {#关键实现}

### Send an image and text {#发送图片--文本}

```c
// 1. Send the prompt text first (fin=0: more data follows)
stm_open_data_t text_data = {0};
text_data.event_id       = event_id;
text_data.data_type      = STM_DATA_TYPE_TEXT;
text_data.payload        = (uint8_t *)prompt;
text_data.payload_length = strlen(prompt);
stm_open_session_send(session, &text_data, 0);

// 2. Send the image (fin=1: this request is complete)
stm_open_data_t img_data = {0};
img_data.data_type = STM_DATA_TYPE_IMAGE;
img_data.image_params = (stm_image_params_t){
    .payload_type = 0,      // Raw binary
    .format       = 1,      // JPEG
    .width        = width,
    .height       = height,
};
img_data.payload        = image_buf;
img_data.payload_length = image_len;
stm_open_session_send(session, &img_data, 1);
```

### Receive structured responses {#接收结构化响应}

The following is literal example output; the Chinese response text is preserved:

```
[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":0,"data":{"content":
  "{\"type\":\"card\",\"name\":\"谷歌浏览器图标\",\"pinyin\":\"gu ge liu lan qi tu biao\",
  \"relatedWord\":\"Google Chrome Icon\"}",...}}

[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":0,"data":{"content":
  "谷歌浏览器（Google Chrome）是谷歌公司开发的一款全球流行的网页浏览器...",...}}

[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":1,"data":{"content":"",
  "finish":true,...}}
```

- The first text packet contains **structured JSON** (card data). The Chinese `name` means "Google Chrome icon".
- The second text packet contains a **natural-language description**: "Google Chrome is a globally popular web browser developed by Google...".
- `eof=1` together with `finish=true` marks the end of the text stream.

TTS audio packets arrive as well and are saved to the output file.

## Integration with platform workflows {#与平台工作流的配合}

This example depends on a **Workflow** configured on the Tuya AI platform. The prompt text (such as `"image_recognition"`) serves as a selector match condition in the workflow, routing the input to the corresponding processing branch.

For detailed workflow configuration instructions, see [Create a workflow](../guides/create-workflow).

## Image requirements {#图片要求}

| Parameter | Limit |
|------|------|
| Format | JPEG or PNG |
| Size | Up to 10 MB |
| Transfer method | Raw binary (payload_type=0) |

## Considerations {#注意事项}

- The prompt text must match the selector configuration in the platform workflow, or it might not trigger the correct processing flow.
- Obtain device credentials through Provisioning. The default credentials in the example are for testing only.
- The image `width` and `height` parameters are illustrative; set them to the actual image dimensions in your application.
- The current example sends the text prompt first, then sends the entire image at once. The "chunk count" in the logs is for display only; it does not mean that the image is actually fragmented into 10 KB chunks.
- `audio_path` only initializes the output filename. When the first audio packet arrives, the example changes the filename to `output_tts.<ext>` based on the audio format returned by the cloud.
- The AI response timeout is 60 seconds, and the connection establishment timeout is 10 seconds.
- The output audio format (filename extension) is determined automatically from the first audio packet's parameters returned by the cloud.
