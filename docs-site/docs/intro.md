---
title: Tuya Agentic-kit 介绍
sidebar_label: 介绍
sidebar_position: 1
slug: /intro
---

# Tuya Agentic-kit 介绍

Tuya Agentic-kit 是用于智能硬件接入 TuyaAI 平台的多模态端侧 SDK，支持对话、图
片理解/生成，并且支持设备侧 MCP，功能强大灵活，对话低延迟，可应用于各类 AI 硬
件场景。

- **多模态 AI**：语音对话、图片理解、图片生成、设备侧 MCP 指令
- **全球化**：设备默认连接用户所在地的数据中心，天然支持出海
- **跨平台**：芯片和操作系统无关，支持 macOS/Linux、FreeRTOS，支持 x86、MIPS、ESP32 等

## 接入流程

智能硬件接入 Tuya 平台需要至少两类信息：

- **产品 PID**：配置一类设备的共同信息，包括功能点、面板、绑定的 AI Agent 等。
- **设备授权码**（uuid / authkey）：每台设备独立持有，用于激活并换取云端凭据。

对于从头开始的开发者，大致流程如下：

1. 注册 Tuya IoT 平台账号，创建产品，并绑定或创建 Agent
2. 从 Tuya IoT 平台获取设备授权码（uuid、authkey）
3. 通过配网操作激活授权码，云端生成 `devid`、`secret_key`、`local_key`
4. 使用 `devid`、`secret_key`、`local_key` 连接 AI 基座

如果需要大规模出货，请联系 Tuya 商务购买授权码；测试阶段可在 Tuya IoT 平台申请免费测试授权码。

## 下一步

- [系统架构](./architecture) — 了解 SDK 模块、配网方式及数据流
- [快速开始](./tutorials/quick-start) — 运行第一个示例
- [SDK 参考](./reference/rtc-tcp-client) — 查阅 API 文档
