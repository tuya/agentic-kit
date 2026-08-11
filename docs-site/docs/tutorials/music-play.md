---
title: 音乐播放
sidebar_label: 音乐播放
sidebar_position: 3
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

示例的 `on_text` 回调做两件事：NLG 文本逐片即时打印（保持流式体验），同时把所有分片累积起来，流结束后再解析 SKILL 结构：

```c
static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    /* NLG 文本：每个分片自成一行 JSON，到达即打印（按 msg->len 截断，
       并解码 \n / \" / \uXXXX 转义）。返回 1 表示这片是 NLG 且已处理，
       包括 {"content":""} 这样的空结束片——它仍然是 NLG，不能再按原样打印 */
    if (nlg_print_content(msg->text, msg->len))
        dc->stream_printed = 1;

    /* 同时累积整个流：SKILL 响应是一份 JSON，可能跨分片，拼完整才解析 */
    if (demo_textbuf_accum(&dc->text, msg) == 1)
        handle_complete_text(dc);   /* is_music_response → try_parse_music */
}
```

服务端可能不发独立的文本 END 分片（SDK 会丢弃空文本帧），因此 `on_event` 在收到 `TAI_EVT_END`（回合结束）时调用 `demo_textbuf_flush()` 兜底交付缓冲中的流。

:::caution 两个必须注意的约束
- **`msg->text` 没有 `\0` 结尾**。`tuya_ai.h` 中明确标注该指针借用自 SDK 接收缓冲区且非 NUL 结尾，对它直接调用 `strstr` / `strchr` / `strcmp` 会越过 `msg->len` 读到上一个数据包的残留字节。所有解析都必须先按 `msg->len` 把数据拷出来。
- **文本按 `stream_flag` 分片下发**（`TAI_STREAM_START` / `MIDDLE` / `END`，或单个 `ONE_SHOT`）。只做打印的场景可以逐片处理，但解析 JSON 结构必须先重组整个流，否则 `"code":"music"` 与 `audios` 可能落在不同分片里。

这两件事由 `demo_text.h` 的 `demo_textbuf_accum()` / `demo_textbuf_flush()` 统一处理；断线重连前用 `demo_textbuf_reset()` 丢弃旧连接的半截流。
:::

:::info 缓冲区只装一条流
`demo_textbuf_t` 一次只重组一条文本流，也**无法**分离回合内交错的两条流——`tai_text_msg_t` 里没有可用于分路的字段：同一回合内所有文本包共享同一个 `event_id`（SDK 只 latch 一个回合 id，`TAI_EVT_END` 后清空）和同一个 `data_id`（`TAI_DATA_ID_TEXT_DOWN`）。

能做的是**察觉**，分两种情况：

**一条流被新流顶掉**——上一条流还没收到 END，就来了 `START` / `ONE_SHOT`。缓冲区只装一条流，旧的那条必然丢失，示例把它计入 `tb->dropped` 并打印告警，而不是无声丢弃：

```
[demo_text] a new stream started while 214 bytes of the previous one were still buffered: dropping those — ...
```

**`seq` 出现缺口**——`seq` 是回合内的文本包计数器，缺口说明有应用没看到的分片消耗了序号。但这个信号是**有歧义**的：SDK 自己会丢弃零长度文本帧（`tai_protocol.c` 的 `media_text()` 仅在 `payload_len > off` 时上抛），这类帧不携带任何字节，跨过它们拼出来的正是那份正确的文档；只有当缺失的分片属于另一条交错的流时，继续拼接才会把两份文档混在一起——而那种混合物随后会在 JSON 解析处被拒。因此默认策略是**打印告警后继续累积**：

```
[demo_text] text seq gap (11 -> 13): continuing — ...
```

若某个部署里交错才是更可能的原因，用 `-DDEMO_TEXT_SEQ_CHECK=2` 改为遇缺口即丢流；`-DDEMO_TEXT_SEQ_CHECK=0` 完全关掉该检查。

无论哪种丢失，`demo_textbuf_t.dropped` 都会累加（`demo_textbuf_reset()` 不会清零它），示例在退出前据此判定成败——丢了流却报告"本次查询没有音乐响应"并返回 0，会让脚本把丢数据的运行当成成功。
:::

另外，`code` 字段要在 SKILL 信封的 `data` 对象里取，而不是在整份文档里取第一个匹配——外层常见的 `{"code":0,"msg":"ok","data":{"code":"music",...}}` 结构会让"取第一个 code"拿到状态码 `0`，从而静默丢弃这条音乐响应。

## 公共辅助头文件

`examples/posix/ai/rtc-tcp-client/` 下的五个示例共用三个头文件，避免各自复制一份解析代码：

| 头文件 | 内容 |
|--------|------|
| `demo_json.h` | 极简 JSON 读取（字符串感知的括号配对、`\"` / `\/` / `\uXXXX` 转义解码）、Base64 解码、session token 解析、定长配置字段的有界拷贝 |
| `demo_text.h` | `tai_text_msg_t` 的安全处理：按长度截断的查找、NLG 正文解码打印、文本流重组与丢流记账 |
| `demo_mcp.h` | 设备端 MCP 应答：回显请求 `id`、按 method 返回正确形状；无工具设备用 `demo_mcp_reply_no_tools()` |
| `demo_reconnect.h` | 应用侧重连策略（指数退避 + 熔断器） |

`demo_json.h` 中的所有函数都要求传入 **以 `\0` 结尾** 的缓冲区；回调里的 `msg->text` / `msg->data` 需要先拷贝。

## NLG 文本输出

非音乐响应的 NLG 文本（AI 的语音回复文字）会按文本流逐段打印。示例用 `nlg_print_content()` 从 JSON 中提取 `content` 字段，**解码其中的 JSON 转义**后仅输出文本内容——服务端常把中文写成 `\uXXXX`，不解码的话终端上看到的是 `你好` 而不是「你好」：

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
- 本示例声明了 MCP 支持但未实现任何工具，`on_event` 收到 `TAI_EVT_MCP_CMD` 时调用 `demo_mcp.h` 的 `demo_mcp_reply_no_tools()` 作答。注意 **SDK 的内置默认属性本来就打开 MCP**，所以不传 `session_attrs_json` 的设备同样会收到 MCP 请求，必须能正确应答。要实现真正的设备工具请参考 `mcp_demo.c`。
