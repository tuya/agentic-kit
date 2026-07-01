---
title: Tuya Agentic-kit 介绍
sidebar_label: 介绍
sidebar_position: 1
slug: /intro
---

# Tuya Agentic-kit 介绍

Agentic-kit 是一个 **C 语言端侧 SDK** —— 你将它编译进设备固件，使硬件产品获得
AI 语音对话、图片理解/生成、以及设备侧 MCP 工具调用能力。它连接设备与涂鸦 AI
云平台，处理从配网激活到实时音视频交互的完整通信链路。

> 💡 **没有硬件？**
> 你可以在 macOS 或 Linux 上直接运行全部 AI 示例（语音对话、图片理解、MCP 工具
> 调用），无需开发板。示例内置测试设备凭据，从[快速开始](./tutorials/quick-start)
> 开始即可。

## 核心特性

- **多模态 AI**：语音对话、图片理解、图片生成、设备侧 MCP 指令
- **全球化**：设备默认连接用户所在地的数据中心，天然支持出海
- **跨平台**：芯片和操作系统无关，支持 macOS/Linux/FreeRTOS 等系统，
    ESP32/ARM/MIPS 及各种硬件芯片
- **轻量**
    * 仅包含和 Tuya 云端的协议实现，无业务入侵
    * C 语言实现，在仅几百 KB RAM 的嵌入式设备上即可运行

## 接入流程

智能硬件接入涂鸦 AI 平台需要两类信息：

| 前提条件 | 说明 | 去哪里获取 |
|----------|------|-----------|
| **产品 PID** | 标识一类设备的共同配置（功能点、面板、绑定的 AI Agent） | [创建和配置 Agent](./guides/create-agent) |
| **设备授权码**（uuid / authkey） | 每台设备独立持有，用于激活并换取云端凭据 | [领取授权码](./get-authkey) |

对于从头开始的开发者，大致流程如下：

1. 在 [Tuya IoT 平台](https://iot.tuya.com) 创建产品、配置 AI Agent
   → 详见 [创建和配置 Agent](./guides/create-agent)
2. 从 IoT 平台获取设备授权码（uuid + authkey）
   → 详见 [创建 Agent](./guides/create-agent) 第 4 步
3. 通过配网激活授权码（四种方式任选其一）
   → 详见[配网方式总览](./tutorials/pair-overall)
4. 使用激活后的凭据（`devid`、`secret_key`、`local_key`）连接 AI 基座
   → 详见[快速开始](./tutorials/quick-start)

:::note 大规模出货
如需大规模出货，请联系涂鸦商务购买授权码；测试阶段可以使用免费领取的测试
授权码。
:::

## 下一步

- [核心概念](./concepts) — 了解设备激活、配网、AI Agent、tRTC、数据点等关键概念
- [系统架构](./architecture) — 了解 SDK 模块组成与项目结构
- [快速开始](./tutorials/quick-start) — 运行第一个示例
- [SDK 参考](./reference/rtc-tcp-client) — 查阅 API 文档
