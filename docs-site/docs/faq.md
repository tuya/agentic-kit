---
title: 常见问题（FAQ）
sidebar_label: FAQ
sidebar_position: 100
slug: /faq
---

# 常见问题（FAQ）

## Agentic-kit 和 TuyaOpen 的区别是什么，如何选择？

**Agentic-kit** 是专注于 AI 能力接入的轻量级 C SDK，核心功能是语音聊天、图片理解、MCP 设备命令。体积小、依赖少，适合在 RTOS 或裸机环境运行。

**TuyaOpen** 是涂鸦的全功能 IoT SDK，覆盖 WiFi 配网、OTA 升级、DP 数据点控制、场景联动、设备管理等完整 IoT 能力，体积较大。

**选择建议：**

| 产品类型 | 推荐方案 |
|----------|----------|
| AI 交互是核心功能（AI 音箱、拍学机、AI 机器人） | Agentic-kit |
| 传统智能设备，附加 AI 能力（智能灯 + 语音控制） | TuyaOpen + AI 插件 |
| 需要完整 IoT 管理 + AI | TuyaOpen 为主，Agentic-kit 模块为辅 |
| 资源受限的 MCU（RAM < 512KB） | Agentic-kit（更轻量） |

## 如何请求 RTC Client 库支持新的平台/架构？

RTC Client（`stm_open_*`）以预编译静态库形式提供，当前支持：

- macOS arm64
- Linux x86_64 / aarch64
- Linux ARM (Rockchip830)
- MIPS (Ingenic)

**如需新平台：**

1. 准备信息：目标平台名称、工具链信息（`gcc -v` 输出）、target triple、BSP 链接
2. 通过涂鸦技术支持或项目负责人提交请求
3. 通常 1-2 周内可交付新平台的预编译库

**替代方案：** 使用 [RTC TCP Client](./reference/rtc-tcp-client)（源码形式），只需实现 `pal.h` 接口即可在任意平台运行。详见[适配新平台](./guides/porting-to-new-platform)。

## 设备端是否需要 VAD？

**不是必须的。** 云端提供 Server VAD 能力（`TAI_EVT_SERVER_VAD`），设备端持续发送音频即可，云端检测到用户停止说话后会通知设备。

**但推荐以下场景使用设备端 VAD：**

| 场景 | 建议 |
|------|------|
| 电池供电设备 | 设备端 VAD 避免持续传输静默音频 |
| 带宽受限（如 2G/NB-IoT） | 设备端 VAD 减少上行数据量 |
| WiFi 音箱、持续供电设备 | 仅云端 VAD 即可 |
| 高交互体验要求 | 混合：设备端做粗判，云端做精判 |

详见[VAD 与打断](./guides/vad-and-interrupt)。

## RTC TCP Client 和 RTC Client 的区别？用哪个？

| | RTC TCP Client (`tai_*`) | RTC Client (`stm_open_*`) |
|---|---|---|
| 集成方式 | 源码 + PAL | 预编译静态库 |
| 协议 | tRTC(tuya自研RTC协议)，TCP 实现 | tRTC(tuya自研RTC协议)，UDP 实现 |
| API 风格 | 类型化（`send_text`、`send_audio_*`） | 通用 data 结构体 |
| 平台扩展 | 实现 PAL 即可 | 需要等待官方编译新平台库 |

UDP实现版本有非常好的弱网性能支持, 即使在网络条件很差的情况下仍能很好的工
作。 如果你有弱网条件的使用场景, 则优先选用rtc-client。

其他场景下看各人喜好。

## 支持哪些音频格式？

**上行（设备 → 云端）：**
- PCM：16kHz / 16-bit / mono（推荐，实现简单）
- Opus：16kHz / mono（推荐带宽受限场景）

**下行（云端 → 设备）：**
- 由云端 Agent TTS 配置决定
- 通常为 PCM 或 Opus
- 格式信息在首个音频回调中提供（`sample_rate`、`frame_duration`）

详见[音频格式配置指南](./guides/audio-format)。

## session_token 过期了怎么办？

**RTC Client：**
- Token 有效期通常为 12-24 小时
- 过期后会收到 `STM_ETOKEN_EXPIRED` 错误或 `on_state` 回调
- 重新调用 `iot_client_get_session_token()` 获取新 token，然后创建新 session

**RTC TCP Client：**
- 使用长连接 + Ping/Pong 保活，不依赖 session_token
- 如果连接断开（`on_disconnect` 回调），重新调用 `tai_connect()` 即可
- 连接刷新由协议内部自动处理（`CONNECTION_REFRESH_REQ/RESP`）

## 设备需要什么样的网络条件？

| 指标 | 最低要求 | 推荐 |
|------|---------|------|
| 带宽（上行） | 32 kbps (Opus) / 256 kbps (PCM) | 512 kbps+ |
| 带宽（下行） | 64 kbps | 512 kbps+ |
| 延迟 | < 500ms RTT | < 200ms RTT |
| 稳定性 | 偶尔丢包可恢复 | 稳定 WiFi/以太网 |

- 使用 Opus 编码可大幅降低带宽要求
- 不支持离线模式——需要持续的网络连接
- 支持断线重连（RTC Client 有 Recovering 状态，RTC TCP Client 需应用层重连）
