---
title: 拍学机（图片理解）
sidebar_label: 图片理解
sidebar_position: 3
---

# 拍学机（图片理解）

> 对应示例：
>      * `examples/posix/ai/rtc-client/`
>      * `examples/posix/ai/rtc-tcp-client/`


:::tip
本章以预编译库版 rtc-client（`stm_open_*` API）为例进行说明。rtc-tcp-client 的实现逻辑类似，请参考 `examples/` 下的对应源码。
:::

本章介绍图片理解示例的功能与实现。该示例演示了如何通过 Agentic-kit 实现图片理解功能——发送一张图片和文本 prompt 给 AI，接收结构化 JSON 结果和 TTS 语音回复。

:::note 前置条件
- 设备凭据（`devid`、`secret_key`、`local_key`）—— 示例内置默认测试凭据，可直接运行。用自己的设备时需先完成[配网](./scan-by-device)获取凭据。
- 此示例依赖在 Tuya AI 平台上配置的**工作流**（Workflow），详见[创建工作流](../guides/create-workflow)。
:::

## 功能概述

edu-camera 示例模拟了 **拍学机** 的典型场景：

1. 用户按下拍照按钮，设备拍摄一张照片
2. 设备将照片和触发指令（prompt）发送给云端 AI
3. AI 返回结构化 JSON（包含名称、拼音、英文等卡片信息）和 TTS 语音播报

示例会：
- 读取本地图片文件（JPEG 或 PNG，最大 10MB）
- 先发送文本 prompt（`fin=0`），再发送图片数据（`fin=1`）
- 在控制台实时打印 AI 返回的文本/JSON 内容
- 将 TTS 音频保存到本地文件，并根据首包音频参数自动判断输出格式（PCM/MP3/OGG 等）
- 统计并输出延迟信息

## 运行方式

```sh
# 在 examples/posix 目录下运行（默认相对路径 res/test.jpg 生效）
./build/edu_camera_demo

# 在任意目录下运行时，传入图片绝对路径
./build/edu_camera_demo /absolute/path/to/test.jpg "image_recognition"

# 完整参数
./build/edu_camera_demo <img_path> <prompt> <audio_path> <devid> <secret_key> <local_key>
```

## 关键实现

### 发送图片 + 文本

```c
// 1. 先发送 prompt text（fin=0，还有后续数据）
stm_open_data_t text_data = {0};
text_data.event_id       = event_id;
text_data.data_type      = STM_DATA_TYPE_TEXT;
text_data.payload        = (uint8_t *)prompt;
text_data.payload_length = strlen(prompt);
stm_open_session_send(session, &text_data, 0);

// 2. 再发送图片（fin=1，本次请求结束）
stm_open_data_t img_data = {0};
img_data.data_type = STM_DATA_TYPE_IMAGE;
img_data.image_params = (stm_image_params_t){
    .payload_type = 0,      // raw 二进制
    .format       = 1,      // JPEG
    .width        = width,
    .height       = height,
};
img_data.payload        = image_buf;
img_data.payload_length = image_len;
stm_open_session_send(session, &img_data, 1);
```

### 接收结构化响应

```
[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":0,"data":{"content":
  "{\"type\":\"card\",\"name\":\"谷歌浏览器图标\",\"pinyin\":\"gu ge liu lan qi tu biao\",
  \"relatedWord\":\"Google Chrome Icon\"}",...}}

[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":0,"data":{"content":
  "谷歌浏览器（Google Chrome）是谷歌公司开发的一款全球流行的网页浏览器...",...}}

[Text] {"bizId":"img_understand_001","bizType":"NLG","eof":1,"data":{"content":"",
  "finish":true,...}}
```

- 第一个文本包包含 **结构化 JSON**（卡片数据）
- 第二个文本包包含 **自然语言描述**
- `eof=1` 且 `finish=true` 表示文本流结束

同时还会收到 TTS 音频包，保存到输出文件中。

## 与平台工作流的配合

此示例依赖在 Tuya AI 平台上配置的 **工作流**（Workflow）。prompt 文本（如 `"image_recognition"`）在工作流中充当选择器的匹配条件，用于路由到对应的处理分支。

关于工作流的详细配置方法，请参考[创建工作流](../guides/create-workflow)。

## 图片要求

| 参数 | 限制 |
|------|------|
| 格式 | JPEG 或 PNG |
| 大小 | 最大 10MB |
| 传输方式 | 原始二进制（payload_type=0） |

## 注意事项

- prompt 文本必须和平台工作流中的选择器配置一致，否则可能无法触发正确的处理流程。
- 设备凭据需要通过配网流程获取，示例中的默认凭据仅供测试使用。
- 图片的 `width` 和 `height` 参数是示意值，实际使用时应设置为真实图片的尺寸。
- 当前示例会先发送文本 prompt，再一次性发送整张图片；日志中的“分包数”仅用于打印显示，并不表示图片已按 10KB 真正分片发送。
- `audio_path` 仅用于初始化输出文件名；首包音频到来后，示例会根据云端返回的音频格式改写输出文件名为 `output_tts.<ext>`。
- AI 响应的最大等待时间为 60 秒，连接建立的最大等待时间为 10 秒。
- 输出音频文件的格式（后缀）会根据云端返回的首包音频参数自动判断。
