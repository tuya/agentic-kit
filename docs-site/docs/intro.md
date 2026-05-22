---
title: Tuya Agentic-kit 介绍
sidebar_label: 介绍
sidebar_position: 1
slug: /intro
---

# Tuya Agentic-kit 介绍

## 功能介绍

Tuya Agentic-kit 是用于智能硬件接入 TuyaAI 平台的多模态端侧 SDK，支持对话、图
片理解/生成，并且支持设备侧 MCP，功能强大灵活，对话低延迟，可应用于各类 AI 硬
件场景。

Tuya AI 平台是全球化平台，支持设备连接到全球不同的数据中心（在配网时由配网用
户所在地决定），默认支持设备的出海。

Tuya Agentic-kit 是芯片和操作系统无关的，既支持 macOS/Linux，也支持 FreeRTOS，既支持
x86 的，也支持 MIPS、ESP32 等芯片的。（当前只支持部分，不同操作系统和芯片的适配正在
进行中）。



## 接入流程

智能硬件要接入 Tuya 平台，需要至少两类信息

* **产品 PID**，在上面配置一类设备的共同信息，比如产品的功能点、产品的面板，
    以及产品的 AI Agent 等，这些都是配置在产品上的。
* **设备授权码**：有了 PID 之后，硬件还需要设备的授权码才可以连到云平台。

对于一个从头开始的开发者来说，大致的流程是：

* 注册 Tuya IoT 平台账号，创建产品，并绑定或者创建 Agent
* 从 Tuya IoT 平台获取设备授权码信息（uuid、authkey）
* 通过 App 配网操作，激活授权码，在激活时，Tuya 云端会生成 devid、secret_key、
    local_key 用于后续设备和云端交互。
* 通过 devid、secret_key、local_key 来连接 AI 基座。

如果需要大规模出货，需要联系 Tuya 的商务购买授权码。客户可以在 Tuya IoT 平台申
请免费的测试用授权码。

## 配网方式总览

新设备首次使用前，需要通过配网操作将设备与涂鸦云的 App 账号绑定，并激活授权码。当前支持的配网方式：

| | 设备扫 App 二维码 | 设备展示二维码 App 扫 | OpenAPI Token 激活 | BLE 蓝牙配网 |
|---|---|---|---|---|
| 依赖涂鸦 App | 是 | 是 | **否** | 是 |
| 设备硬件要求 | 摄像头 | 屏幕 | 无特殊要求 | BLE |
| Token 来源 | App 二维码中 | 云端 MQTT 推送 | OpenAPI 返回 | App BLE 传递 |
| 网络信息传递 | 二维码含 WiFi 凭据 | 设备需自行联网 | 设备需自行联网 | BLE 传递 WiFi 凭据 |
| 适用场景 | 带摄像头的设备 | 带屏设备 | 自有 App / 产线激活 | ESP32 等带 BLE 的设备 |
| 对应教程 | [设备扫码配网](./tutorials/scan-by-device) | [App 扫码配网](./tutorials/scan-by-app) | [OpenAPI 配网](./tutorials/openapi-activate) | [BLE 配网](./tutorials/pair-by-ble) |

激活成功后，云端会为设备分配 `devid`、`secret_key`、`local_key`，后续使用 AI SDK 时均需使用这三个字段。

## SDK 模块

Agentic-kit 包含以下核心模块：

| 模块 | 头文件 | 说明 |
|------|--------|------|
| **RTC TCP Client** | `tuya_ai.h` | tRTC(tuya自研RTC协议) TCP 实现。源码级集成，PAL 可移植 |
| **RTC Client** | `stm_open.h` | tRTC(tuya自研RTC协议) UDP 实现，预编译库形式提供，支持多平台 |
| **IoT Client** | `iot_client.h` | 设备激活、MQTT 连接、会话令牌获取 |
| **Tuya BLE** | `tuya_ble_prov.h` | BLE 配网模块（ESP-IDF） |

### RTC TCP Client 使用

RTC TCP Client（`tuya_ai.h`）是推荐的 AI 通信模块，使用流程简洁：

```text
tai_ctx_init(mem, &cfg)              // 初始化上下文，设置回调
tai_connect(ctx)                     // TLS 连接 + 启动后台接收线程
        |
        v
tai_send_text(ctx, text, len)        // 发送文本
tai_send_audio_start(ctx, ...)       // 开始发送音频流
tai_send_audio_chunk(ctx, pcm, len)  // 发送音频帧
tai_send_audio_end(ctx)              // 结束音频流
        |
        v
（通过 on_audio / on_text / on_event 回调接收响应）
        |
        v
tai_disconnect(ctx)
tai_ctx_deinit(ctx)
```

更详细的 API 说明，请参考 [RTC TCP Client 参考文档](./reference/rtc-tcp-client)。

### RTC Client 使用

RTC Client（`stm_open.h`）主要用于 AI 功能的实现，包括语音聊天、图片识别、图片生成等。它的主要
使用流程如下：

```text
stm_open_init(config)                    // 初始化 SDK，设置日志回调
stm_open_set_log_level(level)            // 可选，设置日志级别
        |
        v
stm_open_session_create(session_config)  // 用 token + local_key 创建会话
        |                                // 注册 on_state 和 on_data_recv 回调
        v
（通过 on_state 回调等待连接状态变为 CONNECTED）
        |
        v
stm_open_session_send(session, data, fin)  // 发送数据（文本/音频/图片）
        |                                   // fin=1 表示输入结束
        v
（通过 on_data_recv 回调接收响应：文本、音频、命令）
（回调中 fin=1 表示响应结束）
        |
        v
stm_open_session_close(session)
stm_open_deinit()
```

更详细的功能介绍，请参考 RTC Client 的[参考文档](./reference/rtc-client)。
