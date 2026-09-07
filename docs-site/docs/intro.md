---
title: Tuya Agentic-kit 介绍
sidebar_label: 介绍
sidebar_position: 1
slug: /intro
---

import Link from '@docusaurus/Link';

# Tuya Agentic-kit 介绍

<div className="doc-lead">Agentic-kit 是面向 Physical AI 终端的原子级轻量 C SDK。把它接入既有固件、操作系统或应用，即可建立终端与 Tuya Physical AI 平台之间的设备身份、IoT 管理和实时多模态通道。</div>

<div className="doc-summary">
  <div><b>WHERE IT RUNS</b><span>MCU、RTOS、Linux SoC、PC、手机 App 与具备原生适配层的浏览器环境</span></div>
  <div><b>WHAT IT CONNECTS</b><span>音频、图像、视频、文本、事件、指令与工具调用</span></div>
  <div><b>HOW IT INTEGRATES</b><span>芯片无关、平台抽象、原子选用，保留原有产品工程与核心业务</span></div>
</div>

## 它解决什么问题

Physical AI 产品需要让真实硬件持续感知环境、理解用户并执行动作。端侧要面对资源、功耗、网络和外设差异，云端则承担实时通信、Agent 编排、模型、记忆、知识和行业服务。Agentic-kit 位于两者之间，把接入所需的身份、连接、会话和数据交互收敛为一套轻量接口。

<div className="doc-callout"><b>能力边界</b><p>Agentic-kit 负责端侧接入与数据、事件、结果的可靠流转。模型、算法、记忆和行业服务按产品需要在端侧、涂鸦平台或客户自有服务中部署与组合。</p></div>

## 核心能力

<div className="doc-card-grid doc-card-grid--three">
  <article><span>01 / IDENTITY</span><h3>设备激活与身份</h3><p>以产品 PID 和设备授权码完成激活，获得设备连接平台所需的正式凭据。</p></article>
  <article><span>02 / REALTIME</span><h3>tRTC 实时交互</h3><p>建立端云加密长连接，流式传输多模态数据，并接收 AI 结果与会话事件。</p></article>
  <article><span>03 / MULTIMODAL</span><h3>多模态数据</h3><p>上行音频、图像、视频、文本与事件；下行语音、文本、图像、视频和指令。</p></article>
  <article><span>04 / TOOLS</span><h3>设备侧 MCP</h3><p>把读传感器、控制外设等终端能力注册为工具，由 Agent 按明确 Schema 调用。</p></article>
  <article><span>05 / IOT</span><h3>设备管理与 DP</h3><p>提供激活、MQTT 连接、会话令牌和数据点上下行等 IoT 基础能力。</p></article>
  <article><span>06 / PORTABLE</span><h3>PAL 跨平台适配</h3><p>通过网络、线程、互斥锁、时间和内存接口适配不同芯片与操作系统。</p></article>
</div>

## 一套入口，多种终端

Agentic-kit 不限定产品形态。相同的接入模型可以进入语音玩具、拍学设备、宠物陪伴、办公效率终端、空间助手与具身机器人，也可以先在 macOS 或 Linux 上验证，再移植到目标硬件。

| 终端形态 | 典型输入 | 典型输出或动作 | 可组合的平台能力 |
| --- | --- | --- | --- |
| 语音与陪伴设备 | 音频、按键、状态事件 | TTS、文本、灯效与表情 | ASR、VAD、Agent、长期记忆 |
| 视觉与学习设备 | 图像、视频、语音 | 识别结果、讲解、屏幕内容 | 视觉理解、物体识别、知识服务 |
| 效率终端与应用 | 会议音频、文件、屏幕事件 | 摘要、行动项、工具调用 | Agent、知识库、客户业务工具 |
| 具身机器人 | 语音、视觉、环境与本体状态 | 移动、抓取、设备联动、回执 | 任务规划、PAM、MCP、已授权设备生态 |

## 接入流程

<div className="doc-flow">
  <div><strong>创建产品</strong><span>在 Tuya IoT 平台获得产品 PID，并配置默认或可切换的 AI Agent。</span></div>
  <div><strong>准备身份</strong><span>测试阶段领取 uuid / authkey；量产阶段按项目获取授权码。</span></div>
  <div><strong>激活并连接</strong><span>完成配网，持久化 devid、secret_key 与 local_key，建立 IoT 与 tRTC 通道。</span></div>
  <div><strong>接入产品逻辑</strong><span>处理流式数据、会话事件与工具调用，再按需要连接模型和业务服务。</span></div>
</div>

<div className="doc-callout doc-callout--info"><b>没有硬件也可以开始</b><p>官方示例支持在 macOS 或 Linux 直接验证语音、图片和 MCP 链路。先跑通 POSIX 示例，再实现目标平台 PAL，能更快分离云端配置与硬件移植问题。</p></div>

## 开始前需要什么

| 信息 | 作用 | 来源 |
| --- | --- | --- |
| 产品 PID | 标识一类产品及其功能点、面板和绑定的 Agent | Tuya IoT 平台创建产品 |
| `uuid` / `authkey` | 每台设备独立持有的出厂身份凭证 | 测试授权码或量产授权 |
| 网络与系统适配 | 提供 TCP、线程、内存等 PAL 能力，或采用已支持的平台实现 | POSIX、FreeRTOS 或自定义 PAL |
| 产品交互设计 | 确定采集、播放、打断、工具执行和异常恢复策略 | 客户产品与固件工程 |

## 从 SDK 连接到平台能力

Tuya Physical AI 平台把端云通信、Agent 编排、模型、记忆、知识、设备控制和垂类感知组织成可组合能力。比如语音链路可组合流式 ASR、端侧或云端 VAD、热词与 TTS；视觉产品可连接物体识别、宠物个体识别或场景理解；客户也可接入自有模型、私域知识与核心云服务。

<div className="doc-boundary">
  <div><h3>Agentic-kit 提供</h3><ul><li>终端身份、激活与 IoT 连接</li><li>端云实时多模态通信</li><li>会话事件、数据回调与工具交互</li><li>跨芯片和操作系统的适配边界</li></ul></div>
  <div><h3>按产品组合</h3><ul><li>端侧或云端感知算法</li><li>Agent、模型、记忆与知识库</li><li>MCP / A2A 与客户自有服务</li><li>设备控制、行业服务与全球运营能力</li></ul></div>
</div>

通用大模型保持开放：客户可以在涂鸦平台选用全球主流模型，也可以接入私有模型。面向 Physical AI 的真实设备场景，涂鸦还训练并持续优化语音、视觉与物理执行等垂直模型，用于改善远场拾音、多人干扰、复杂画面和设备动作等具体体验。模型名称、开放状态和支持平台以正式发布信息为准，垂直模型可通过 [涂鸦算法平台](https://www.tuya.com/model) 查看。

## 为什么连接 Tuya Physical AI 平台

Agentic-kit 保持端侧接入轻量，Tuya Physical AI 平台则提供从实时交互到全球运行所需的通用底座。客户可以只选择需要的部分，并保留模型、数据和核心业务的技术选择权。

<div className="doc-card-grid doc-card-grid--two">
  <article><span>END-TO-END VOICE</span><h3>完整的端到端语音能力</h3><p>把音频采集、VAD、流式 ASR、Agent、记忆、知识库、TTS、播放与打断组织成实时交互链路，减少产品团队自行拼接多家服务的工程成本。</p></article>
  <article><span>OPEN CLOUD &amp; MODEL</span><h3>云与模型保持开放</h3><p>支持选用全球主流模型，也支持客户私有模型、私域知识和核心云服务通过开放能力接入。产品可以混用、替换和逐步演进。</p></article>
  <article><span>GLOBAL &amp; COMPLIANT</span><h3>面向全球运行与合规</h3><p>以全球实时通信、设备身份、加密传输和区域化服务能力支撑产品出海；具体数据处理与合规策略按产品市场和项目配置落地。</p></article>
  <article><span>VERTICAL ALGORITHMS</span><h3>围绕真实硬件训练垂直模型</h3><p>针对远场声音、目标说话人、热词、宠物个体、复杂视觉和物理执行等问题持续优化，让算法指标真正转化为 Physical AI 的用户体验。</p></article>
</div>

## 如何选择端侧路径

**TuyaOS（Wukong）、TuyaOpen 与 Agentic-kit 不是彼此覆盖的三套方案，而是针对不同产品基础和开放深度互相补位。** TuyaOS（Wukong）和 TuyaOpen 由涂鸦定义端侧架构、接口与组件边界，并内置大量连接、设备模型、AI 和通用工程代码；Agentic-kit 则从客户现有架构出发，提供更轻量的原子能力。

<div className="doc-card-grid doc-card-grid--three">
  <article><span>STANDARD / COMMERCIAL</span><h3>TuyaOS（Wukong）</h3><p>采用涂鸦标准化产品框架和量产体系。Wukong 是 TuyaOS 面向 AI 的专项 SDK，适合需要连接、安全、设备模型、AI 与商业化交付协同的产品。</p></article>
  <article><span>STANDARD / OPEN SOURCE</span><h3>TuyaOpen</h3><p>采用涂鸦标准化分层框架，同时开放完整源码与丰富组件。对于从零开始的新项目，通常能以更少的基础工程工作快速进入产品功能开发。</p></article>
  <article><span>FIT YOUR STACK</span><h3>Agentic-kit</h3><p>不要求采用完整的涂鸦端侧框架。它以更小资源和更少侵入接入客户已有技术栈；客户无须等待涂鸦适配目标芯片，可根据芯片平台自行完成 PAL 适配。</p></article>
</div>

| 你的起点 | 优先评估 | 选择原因 |
| --- | --- | --- |
| 希望涂鸦提供商业化 SDK 保障 | TuyaOS（Wukong） | 沿涂鸦标准获得产品开发、量产和 AI 专项能力 |
| 从零开始开发，希望源码开放 | TuyaOpen | 框架和组件较完整，可减少重复建设基础工程 |
| 已有技术栈或自研框架 | Agentic-kit | 从现有架构适配，只增量接入需要的端云能力 |
| 现有芯片尚未被涂鸦现有 SDK 适配 | Agentic-kit | 通过 PAL 从零适配目标平台，不等待完整框架支持 |
| RAM / Flash 受限，要求极低的运行与存储占用 | Agentic-kit | 更轻量、更原子，可按实际功能裁剪 |

<div className="doc-callout doc-callout--neutral"><b>从零项目求快，且无现有技术栈要求，优先看 TuyaOpen；已有技术体系、强调灵活适配，优先看 Agentic-kit</b><p>在目标平台已具备网络栈、编译工具链和必要系统能力，且只完成基础 PAL 与通信接入的条件下，Agentic-kit 最快可在 <strong>半天</strong> 内完成基础适配。音视频外设、配网、产品状态机和量产验证所需时间仍取决于具体方案。</p></div>

## 下一步

- [核心概念](./concepts) - 了解设备激活、Agent、tRTC、数据点与 MCP
- [系统架构](./architecture) - 了解 SDK 模块组成与项目结构
- [快速开始](./tutorials/quick-start) - 运行第一个示例
- [SDK 参考](./reference/rtc-tcp-client) - 查阅 API 文档
