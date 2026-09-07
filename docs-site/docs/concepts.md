---
title: 核心概念
sidebar_label: 核心概念
sidebar_position: 2
slug: /concepts
---

# 核心概念

<div className="doc-lead">先理解身份、连接、会话和能力调用之间的关系，再进入具体 API。Agentic-kit 的设计重点是让终端保持轻量，同时把平台能力与客户自有能力放在清晰、可替换的边界中。</div>

## 身份与激活

<div className="doc-terms">
  <div><div><strong>授权码</strong><small>LICENSE</small></div><section><p>每台设备出厂时持有的身份凭证，由全球唯一的 <code>uuid</code> 和与之配对的 <code>authkey</code> 组成。它用于证明设备有权激活，不等同于激活后的设备 ID。</p><code>出厂身份：uuid + authkey -&gt; 激活成功 -&gt; 云端正式身份：devid + secret_key + local_key</code></section></div>
  <div><div><strong>产品 PID</strong><small>PRODUCT ID</small></div><section><p>标识一类产品的共同配置。PID 下可定义数据点、面板和默认 AI Agent；同一产品的设备共享 PID，但拥有不同授权码与设备 ID。</p></section></div>
  <div><div><strong>设备激活</strong><small>ACTIVATION</small></div><section><p>把设备从出厂状态变为已联网、已绑定用户并已注册云端的过程。激活通常只执行一次，设备应安全持久化返回凭据，后续启动直接使用。</p></section></div>
</div>

## Agent 与会话

<div className="doc-terms">
  <div><div><strong>AI Agent</strong><small>BEHAVIOR</small></div><section><p>定义产品如何理解、决策和表达。平台侧可配置系统提示词、语言、TTS、工作流和可调用工具；一个产品可使用默认 Agent，也可按项目配置切换。</p><code>PID 回答“这是什么产品”，Agent 回答“这个产品如何交流和行动”。</code></section></div>
  <div><div><strong>会话</strong><small>SESSION</small></div><section><p>一次连续 AI 交互的运行上下文。它包含连接状态、输入输出流、回调事件和可选会话属性。SDK 负责承载会话数据；具体业务语义由应用和平台配置共同决定。</p></section></div>
  <div><div><strong>Agent Core</strong><small>PLATFORM RUNTIME</small></div><section><p>Tuya Physical AI 平台中的 Agent 编排与运行层，负责上下文组装、工具编排、模型路由、记忆框架和运行保障。它属于云端平台能力，不随端侧 SDK 编译进设备。</p></section></div>
</div>

## tRTC 实时通道

tRTC 是设备与 Tuya AI 云端之间的加密实时通道。Agentic-kit 当前提供 TCP 开源实现和 UDP 预编译静态库两类客户端，二者在交付方式、平台支持和重连责任上有所不同，应结合目标芯片、弱网需求与源码控制要求选择。

| 实现 | 传输 | 集成形态 | 主要特点 |
| --- | --- | --- | --- |
| `rtc-tcp-client` | TCP | 开源代码 + PAL | 便于源码控制和跨平台移植；应用按文档处理断线与重连 |
| `rtc-client` | UDP | 按芯片平台提供预编译静态库 | 适合快速集成与弱网场景；当前请联系涂鸦商务获取目标平台对应库 |

<div className="doc-callout doc-callout--info"><b>选择前先确认交付形态</b><p>需要源码可控或适配新系统时，优先评估 TCP 开源实现；需要使用 UDP 链路时，应先确认芯片、操作系统、工具链和库版本，再由涂鸦商务提供匹配的预编译静态库。</p></div>

<div className="doc-callout doc-callout--neutral"><b>实时通道不等于 AI 模型</b><p>通道负责把音频、图像、视频、文本、事件和工具结果送到合适的能力，并把回复与指令送回终端。模型和算法可由涂鸦平台、客户云或端侧实现。</p></div>

## 多模态流与事件

<div className="doc-card-grid doc-card-grid--two">
  <article><span>UPLINK / 终端 -&gt; 平台</span><h3>输入与状态</h3><ul><li>流式音频、音频结束</li><li>图像与视频帧</li><li>文本、设备事件、工具执行结果</li><li>设备状态和数据点上报</li></ul></article>
  <article><span>DOWNLINK / 平台 -&gt; 终端</span><h3>结果与动作</h3><ul><li>TTS 音频、文字回复</li><li>图像与视频结果</li><li>会话、VAD 与打断事件</li><li>MCP 工具调用和设备指令</li></ul></article>
</div>

终端应用需要为采集、缓冲、播放、打断和失败恢复建立明确状态机。例如使用云端 VAD 时，设备持续发送音频，收到服务端端点事件后结束本轮上行；本地 VAD 是可选优化，可用于电池、带宽或高交互要求场景。

## PAL：平台抽象层

PAL 是 Agentic-kit 的可移植边界。SDK 不直接绑定某个 RTOS 或芯片，而是通过函数指针使用 TCP、线程、互斥锁、时间和内存。官方提供 POSIX 与 FreeRTOS 实现，新平台可据此适配。

| 类别 | 典型接口 | 工程关注点 |
| --- | --- | --- |
| 网络 | `connect / send / recv / close / poll` | 超时、部分收发、TLS 依赖、断网恢复 |
| 并发 | `thread_create / join`、mutex | 线程栈、回调上下文、锁粒度 |
| 系统 | `time_ms`、`malloc / free` | 时钟单调性、内存峰值与碎片 |

## 物理设备控制：DP 与端侧 MCP

Agentic-kit 提供两种面向物理设备的控制方式。客户可以根据设备能力是否标准化、调用参数是否动态，以及是否需要由 Agent 理解上下文，自主选择数据点或端侧 MCP，也可以在同一产品中分别承载不同类型的能力。

<div className="doc-card-grid doc-card-grid--two">
  <article><span>DATA POINT / DP</span><h3>标准设备能力与状态</h3><p>用产品 Schema 描述开关、亮度、温度、模式等稳定功能，适合 App 控制、状态同步、自动化和跨设备联动。设备通过上报刷新云端状态，通过下行 DP 指令改变本地状态。</p></article>
  <article><span>DEVICE MCP</span><h3>面向 Agent 的动态工具</h3><p>把读传感器、拍照、控制电机或执行任务注册为带名称、说明和参数 Schema 的工具，适合需要上下文理解、结构化参数或执行结果回传的 AI 交互。</p></article>
</div>

| 判断维度 | 优先使用 DP | 优先使用端侧 MCP |
| --- | --- | --- |
| 能力形态 | 稳定、可枚举的设备属性和功能 | 动态、任务型或带复杂参数的工具 |
| 主要调用方 | App、云端自动化、设备联动 | AI Agent |
| 结果表达 | 状态值与标准指令 | 结构化执行结果、错误与上下文 |

<div className="doc-callout doc-callout--info"><b>重连后的状态同步由应用负责</b><p>SDK 不会自动把恢复的本地 DP 状态重新上报。应用应在每次连接或重连成功后刷新必要状态，避免云端和设备端显示不一致。</p></div>

## MCP 的设备侧与云侧角色

MCP 用统一协议描述工具名称、用途、参数和返回值。在 Physical AI 中，工具既可能位于设备，也可能位于客户或第三方云端；端侧 MCP 是上节两种物理设备控制方式之一。

<div className="doc-card-grid doc-card-grid--two">
  <article><span>DEVICE MCP</span><h3>设备是工具提供方</h3><p>终端注册读传感器、控制电机、拍照等能力。Agent 发起调用，设备验证参数并执行，再返回结构化结果。</p></article>
  <article><span>CLOUD MCP</span><h3>云服务是工具提供方</h3><p>天气、搜索、企业知识或客户核心业务由云端服务执行，可使用涂鸦能力，也可接入客户自有实现。</p></article>
</div>

<div className="doc-callout"><b>物理动作需要更严格的边界</b><p>工具声明只是调用入口。具身机器人或空间助手执行门锁、窗帘、移动、抓取等动作前，还需校验设备绑定、用户授权、参数范围、当前状态和失败回退。</p></div>

## 感知、模型与记忆

Physical AI 的体验来自感知、推理、记忆和执行协同。这些能力不必部署在同一位置，也不必由同一家服务提供。

<div className="doc-layers">
  <div><strong>端侧感知</strong><small>EDGE</small><span>关键词唤醒 · 本地 VAD（可选） · 视觉预处理 · 传感器融合 · 安全急停</span></div>
  <div className="doc-layers__accent"><strong>平台能力</strong><small>TUYA PHYSICAL AI</small><span>云端 VAD / ASR / TTS · 视觉算法 · Agent Core · OmniMem · PAM · MCP / A2A</span></div>
  <div><strong>客户能力</strong><small>YOUR STACK</small><span>私有模型 · 私域知识 · 核心云服务 · 自研算法 · 产品业务</span></div>
</div>

例如语音链路可组合流式 ASR、动态热词、云端 VAD 和客户自有 TTS；宠物相机可连接宠物个体识别、行为理解和事件记录；机器人可把平台任务规划与本体控制、安全策略和已授权家庭设备组合。部署位置取决于时延、功耗、带宽、隐私、算力和业务控制权。

## 一张边界表

| 层 | 主要职责 | 由谁控制 |
| --- | --- | --- |
| 产品应用 | 采集、播放、状态机、动作执行、用户体验 | 客户 |
| Agentic-kit | 身份、IoT、tRTC、会话事件、数据与工具交互 | 开放 SDK + 客户集成 |
| Tuya Physical AI 平台 | Agent 编排、平台模型、记忆、知识、设备与行业服务 | 按需选用 |
| 客户服务 | 私有模型、核心业务、私域知识与差异化算法 | 客户，可通过开放能力接入 |

## 下一步

- [系统架构](./architecture) - 了解各模块如何协作
- [快速开始](./tutorials/quick-start) - 在电脑上运行第一个示例
- [创建和配置 Agent](./guides/create-agent) - 在 IoT 平台上准备你的产品和 Agent
