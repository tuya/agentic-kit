---
title: 核心概念
sidebar_label: 核心概念
sidebar_position: 2
slug: /concepts
---

# 核心概念

本页用通俗语言介绍 Agentic-kit 的关键概念，帮助你在进入具体教程之前建立整体
认知。每个概念附有"了解更多"链接，指向详细的指南或 API 参考。

## 授权码（License）

每台设备出厂时持有的"身份证"，用于在涂鸦云端证明"我是合法设备"。

每台设备持有一对**授权码**：

| 字段 | 说明 |
|------|------|
| `uuid` | 设备唯一标识（全球唯一） |
| `authkey` | 授权密钥，与 uuid 配对使用 |

测试阶段可在涂鸦 IoT 平台
[领取授权码](./get-authkey)；大规模出货需联系涂鸦商务购买，
烧录到设备中。

:::note 授权码 ≠ 设备 ID
授权码（uuid/authkey）是出厂时的身份凭证。设备激活后，云端返回的 `devid` 才是
设备在云端的正式 ID——`devid` 内部关联了授权码、产品 PID 和用户账号。
:::


## 设备激活与配网（Activation & Provisioning）

把设备从"出厂状态"变成"已联网、已绑定用户、已注册云端"的完整过程。

这是一次性过程，包含三个环节：联网 → 绑定用户 → 云端注册。设备需要三样东西才能
完成激活（配网）：

| 输入 | 来源 | 作用 |
|------|------|------|
| **uuid / authkey** | 出厂烧录（授权码） | 证明"我是合法设备" |
| **产品 PID** | 授权码绑定 | 告诉云端"我是什么产品"（数据点定义、面板、Agent 等） |
| **用户 Token** | 配网过程获得 | 将设备绑定到用户的涂鸦 App 账号 |

配网成功后，云端验证三项输入，返回：

| 凭据 | 说明 |
|------|------|
| `devid` | 设备在云端的唯一 ID。**内部关联了授权码（uuid/authkey）、产品 PID、用户账号**——之后云端通过 `devid` 就能查到设备的全部身份信息 |
| `secret_key` | 设备与云端通信的加密密钥 |
| `local_key` | 数据点（DP）加解密用的本地密钥 |

激活只需一次。之后设备将这三项凭据持久化存储，每次启动直接使用，无需重复配网。

**了解更多**：[配网方式总览](./tutorials/pair-overall)

## 产品 PID 与 AI Agent

PID 标识"这是哪一类产品"，Agent 决定"这个产品的 AI 怎么表现"。

- **产品 PID**（Product ID）：在涂鸦 IoT 平台创建产品时获得。一类产品共享一个
  PID，PID 下定义了这类产品的数据点（功能点）、面板、绑定的 AI Agent 等。

- **AI Agent**：在涂鸦 AI 平台上配置的智能体，包含系统提示词（System Prompt）、
  TTS 语音类型、语言设置等。一个产品绑定一个默认 Agent（也可绑定多个，通过
  `agent_token` 切换）。

设备通过 SDK 连接 AI 基座时，云端根据设备所属产品的 PID 找到对应的 Agent，
由 Agent 负责处理对话、生成回复、调用工作流等。

**了解更多**：[创建和配置 Agent](./guides/create-agent)

## tRTC 实时通道

设备与涂鸦 AI 云端之间的一条加密长连接，用于实时收发音频、图片、文本。

tRTC（Tuya RTC）是涂鸦自研的实时通信协议。Agentic-kit 提供两种实现：

| 实现 | 传输层 | 集成方式 | 适用场景 |
|------|--------|---------|---------|
| rtc-tcp-client（`tai_*`） | TCP | 源码（需实现 PAL） | 新项目、ESP-IDF、需源码控制 |
| rtc-client（`stm_open_*`） | UDP | 预编译静态库 | 快速集成、弱网场景 |

设备通过 tRTC 通道发送语音/图片/文本给云端 AI，接收 AI 返回的 TTS 音频、文字
回复、以及 MCP 工具调用指令。这条通道由 SDK 内部管理 TLS 加密、心跳保活、
断线重连，你只需实现回调处理业务逻辑。

**了解更多**：[RTC TCP Client 参考](./reference/rtc-tcp-client) | [RTC Client 参考](./reference/rtc-client)

## 数据点（Data Point / DP）

设备功能的数字化表示——每个功能（开关、亮度、温度）对应一个编号的
数据点。

数据点（DP）是涂鸦 IoT 模型中描述设备状态的基本单元。每个 DP 有：

- 一个 **编号**（1–255），如 DP 1 = 开关、DP 2 = 温度
- 一个 **类型**：`bool`（开关）、`value`（数值）、`string`（字符串）、`enum`（枚举）、`raw`（原始字节）
- 一个 **访问模式**：`ro`（只上报）、`rw`（可读可写）、`wr`（只下发）

**Schema** 是一个产品所有 DP 定义的集合（JSON 数组），描述"这个产品有哪些功能、
每个功能的取值范围"。Schema 在 IoT 平台配置产品时生成，设备激活时下发。

SDK 管理数据点的**本地缓存**和**上下行通信**：

- **上行（Report）**：设备 → 云端，设备主动上报当前 DP 值。云端缓存的 DP 状态只能
  通过设备上报来刷新。
- **下行（DP Set）**：云端 → 设备，App 或云端下发指令改变设备状态（如"开灯"）。

:::important SDK 不自动上报
恢复 DP 状态或设备重连后，**SDK 不会自动上报**。应用需在每次（重）连成功后调用
`iot_dp_report_all()` 刷新云端缓存，否则 App 端看到的可能是旧状态。
:::

**了解更多**：[DP 状态持久化](./guides/dp-persistence) | [IoT Client 参考](./reference/iot-client)

## PAL（Platform Abstraction Layer）

一组函数指针（TCP、线程、内存、时间），你为硬件平台实现它，SDK 就能
在上面运行。

Agentic-kit 不直接调用操作系统的 API，而是通过 PAL 间接调用。这使得 SDK 可以
运行在任何平台上——你只需为你的芯片/操作系统实现约 10 个 PAL 接口：

| 接口类别 | 函数 | 说明 |
|----------|------|------|
| TCP | `tcp_connect/send/recv/close/poll` | TCP 连接管理 |
| 线程 | `thread_create/thread_join` | 后台线程 |
| 互斥锁 | `mutex_create/lock/unlock/destroy` | 线程同步 |
| 时间 | `time_ms` | 毫秒时间戳 |
| 内存 | `malloc/free` | 动态内存分配 |

SDK 已提供 POSIX（macOS/Linux）和 FreeRTOS 两个现成的 PAL 实现。移植到新平台
时，复制其中一个作为模板修改即可。

**了解更多**：[适配新平台](./guides/porting-to-new-platform)

## MCP（Model Context Protocol）

让 AI 能调用外部工具（读传感器、控制设备、查数据库等）的标准协议。

MCP 定义了 Client 和 Server 两种角色：**Client 是 AI 侧**（发起工具调用），
**Server 是工具提供方**（执行并返回结果）。涂鸦平台同时支持两种 MCP 功能提供方：

### 设备侧 MCP（设备 = Server，tuya 云端 AI = Client）

设备在云端 AI 面前暴露自己的能力（读传感器、控制外设），AI 在对话过程中主动调用：

```
用户："现在室温多少？"
    → 云端 AI 判断需要调用设备的 read_sensor 工具
    → 设备收到 MCP 请求，读取温度传感器，返回 25.3°C
    → AI 基于结果生成自然语言回复："当前室温 25.3 度"
```

在 Agentic-kit 中，你在设备上注册工具（带名称、描述、参数 schema），AI 通过
JSON-RPC 2.0 调用它们。

**了解更多**：[设备 MCP 指南](./guides/device-mcp)

### 云侧 MCP（三方服务 = Server，tuya 云端 AI = Client）

部分 AI 扩展能力由第三方服务提供，例如天气查询、地理信息查询、联网搜索等。
这些 MCP 工具可在云端的 Agent 配置页面进行选择——可以使用 Tuya 提供的公共
实现，也可以由客户提供自定义实现。

```
用户："现在天气怎么样？"
    → 云端 AI 判断需要调用天气查询 MCP
    → 三方云（如墨迹 / weatherbit 等）处理 MCP 请求，返回当地温度 25.3°C
    → AI 基于结果生成自然语言回复："当前杭州的温度为 25.3 度..."
```
**了解更多**：[创建和配置 Agent](./guides/create-agent)

## 下一步

理解了以上概念后，建议按以下顺序继续：

- [系统架构](./architecture) — 了解各模块如何协作
- [快速开始](./tutorials/quick-start) — 在电脑上运行第一个示例
- [创建和配置 Agent](./guides/create-agent) — 在 IoT 平台上准备你的产品和 Agent
