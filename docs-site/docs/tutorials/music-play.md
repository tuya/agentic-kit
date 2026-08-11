---
title: 音乐播放
sidebar_label: 音乐播放
sidebar_position: 4
---

# 音乐播放

> 对应示例：
>      * `examples/posix/ai/rtc-tcp-client/music_play_demo.c`

:::note 前置条件
- 设备凭据（`devid`、`secret_key`、`local_key`）—— 示例内置默认测试凭据，可直接运行。用自己的设备时需先完成[配网](./scan-by-device)获取凭据。
- 此示例需要系统安装 `curl`（用于下载试听音频片段）。
:::

本章介绍音乐播放示例的功能与实现。该示例演示了如何通过 Agentic-kit 发送文本指令，触发云端 AI 的音乐技能（Music Skill），解析返回的歌曲元数据（歌名 / 歌手 / 专辑 / 音频 URL），并将试听片段下载到本地。

## 功能概述

音乐播放示例模拟了**智能音箱**的典型场景：

1. 用户发送一段文本指令（如"播放周杰伦的歌"）
2. AI 识别意图后触发音乐技能，返回歌曲信息（SKILL 响应）
3. 示例解析歌曲元数据并在控制台展示
4. 通过 `curl` 下载试听音频片段到本地文件

示例会：
- 在控制台实时打印 AI 返回的 NLG 文本内容（流式）
- 检测到音乐 SKILL 响应时，提取并格式化输出歌曲信息
- 下载试听片段到 `output_music.mp3`
- 内置断线重连机制（指数退避 + 熔断器）

## 编译

```sh
cd examples/posix
cmake -S . -B build
cmake --build build --target tai_music_play_demo
```

也可编译全部 POSIX 示例：

```sh
cmake --build build
```

## 运行方式

以下命令均在 `examples/posix` 目录下执行：

```sh
# 默认查询（"播放周杰伦的歌"），使用内置测试凭据
./build/tai_music_play_demo

# 自定义查询
./build/tai_music_play_demo "播放流行音乐"

# 完整参数
./build/tai_music_play_demo [query] [devid] [secret_key] [local_key]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `query` | 文本指令 | `播放周杰伦的歌` |
| `devid` | 设备 ID | 内置测试设备 |
| `secret_key` | 设备密钥 | 内置测试密钥 |
| `local_key` | 本地密钥 | 内置测试密钥 |

运行成功后，控制台输出示例：

```
=== tai_music_play_demo ===
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

示例的退出码可直接用于脚本判断：正常完成一轮对话返回 `0`；连接超时、命中音乐响应但解析失败、或试听片段下载失败返回 `1`。查询未触发音乐技能（AI 只作了文字回复）不算失败。

## 关键实现

此示例基于 rtc-tcp-client（开源 TCP 传输层），使用 `tai_*` API。

### 整体流程

```
iot_client_init → iot_client_get_session_token → parse_token
    → tai_ctx_init → tai_connect → tai_send_text → 等待回调 → tai_disconnect
```

### 1. 获取会话 Token

通过 iot-client 获取包含 TAI 连接信息的 session token：

```c
iot_client_t *iot = iot_client_init(&iot_cfg);

char *token = calloc(1, 4096);
iot_client_get_session_token(iot, NULL, token, 4096);
```

Token 经 Base64 解码后为 JSON，包含连接地址（`connect_conf`）和会话配置（`session_conf`）两部分。

### 2. 初始化 TAI 连接

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

### 3. 发送文本指令

```c
tai_connect(ctx);
tai_send_text(ctx, query, strlen(query));
```

### 4. 解析音乐 SKILL 响应

AI 触发音乐技能后，会通过 `on_text` 回调返回结构化的 SKILL 响应。响应格式如下：

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

示例在 `on_text` 回调中检测 `"code":"music"`，然后逐层提取 `general.data.audios[0]` 中的歌曲字段：

```c
static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    if (strstr(msg->text, "\"code\":\"music\"")) {
        try_parse_music(msg->text, msg->len);
        dc->got_music = 1;
        return;
    }
    /* NLG 文本：仅打印 content 字段 */
    ...
}
```

## NLG 文本输出

非音乐响应的 NLG 文本（AI 的语音回复文字）会流式打印。示例从 JSON 中提取 `content` 字段，仅输出文本内容：

```
Response: 正在为您播放周杰伦的歌
```

## 版权说明

:::note
本节涉及的音乐版权与服务能力由涂鸦内容服务器提供，具体功能与计费策略可能随服务商政策调整，最新信息请参考[音乐故事技能](https://developer.tuya.com/cn/docs/iot/music_tool?id=Keziqxnjdvn6c)。
:::

**试听版歌曲限制**

AI 音乐功能默认返回的是**试听版**歌曲，存在时长限制（通常为 30 秒片段），仅用于功能体验和开发调试。如需在量产产品中播放完整歌曲，需购买对应的音乐高级能力授权。

**网易云音乐接入**

涂鸦内容服务器已接入**网易云音乐**，提供正版音乐资源的搜索与播放。接入流程概述：

1. **购买高级能力**：在[产品开发](https://platform.tuya.com/pmg/list)流程的 **功能定义 > 产品高级功能** 中开通 **AI 播放网易云音乐**，并在[交付物采购](https://platform.tuya.com/purchase/index?type=1)中按设备支付授权费用。授权有效期为 3 年，自设备激活并首次使用起计算。
2. **添加音乐工具**：在智能体开发页面的 **技能配置 > 工具集** 中添加 **音乐故事技能**（包含媒体资源播放控制、查询并播放音乐/儿歌等工具）。
3. **智能体投放**：将关联了音乐技能的智能体投放至已购买高级能力的产品（PID），设备端即可播放完整歌曲。

:::important 网易云音乐注意事项
- 网易云音乐仅支持**中国数据中心**，非中国区设备只能播放试听内容。
- 授权范围不含黑胶 VIP 会员音乐；如需播放会员资源，终端用户需在 App 端设备面板的 **第三方内容授权** 中绑定网易会员账号。
- 网易云音乐功能仅支持**设备端播放**，不支持纯云端播放。
:::

详细的平台配置步骤请参考官方文档：[音乐故事技能](https://developer.tuya.com/cn/docs/iot/music_tool?id=Keziqxnjdvn6c)。

## 注意事项

- 设备凭据需要通过配网流程获取，示例中的默认凭据仅供测试使用。设备 ID 与密钥会写入 `iot_client_config_t` 的定长字段（各 32 字节），超长时示例直接报错退出，不会截断。
- 试听音频的下载依赖系统 `curl`，请确保已安装。示例通过 `fork` + `execvp` 直接调起 `curl`，**不经过 shell**——服务端返回的 URL 只作为一个 argv 元素传入，无法被当作命令执行；同时只接受 `http://` / `https://` 开头的地址。设备端自行实现下载时请沿用这一约束。
- 音乐技能需要在 Tuya AI 平台上正确配置工作流，否则不会返回 SKILL 响应。
- AI 响应的最大等待时间为 60 秒，超时后示例会退出。
- 当前示例仅解析并展示第一首歌曲信息；如需播放完整音频，请在设备端实现音频播放器。
- 元数据展示框按**显示宽度**（中文字符占 2 列）对齐，而非字节数；过长的字段会在字符边界截断。
- `on_audio` 回调在本示例中为空实现，不处理 TTS 音频数据。
