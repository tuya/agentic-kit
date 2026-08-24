---
title: 如何创建和配置 Agent
sidebar_label: 创建 Agent
sidebar_position: 3
---

# 如何创建和配置 Agent

本指南说明如何在 Tuya IoT 平台上创建产品、创建智能体（Agent）并把它**投放**到产品，使设备激活后自动用上这个智能体。平台侧的权威步骤以官方文档为准，本文只负责串起流程、标出每一步对应 SDK 的哪个接口：

| 步骤 | 官方文档 |
|------|---------|
| 创建产品 | [创建产品](https://developer.tuya.com/cn/docs/iot/create-product?id=K914jp1ijtsfe) |
| 配置 AI Agent | [智能体开发平台](https://developer.tuya.com/cn/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud) |
| 把智能体投放到产品 | [智能体投放及费用](https://developer.tuya.com/cn/docs/iot/agent-deploy?id=Kfnx3351272vh)、[产品 AI 功能开发](https://developer.tuya.com/cn/docs/iot/AI-feature?id=Keapy1et1fc63) |

:::tip 还没开始？
如果你还没有产品 PID 和设备授权码（uuid + authkey），请先按本指南操作——这是使用
Agentic-kit 的前提条件。参见[介绍](../intro)中的接入流程。
:::

## 前置条件

- 已注册 [Tuya IoT 平台](https://iot.tuya.com) 账号
- 已了解你的产品形态（音箱、拍学机、机器人等）

## 步骤

### 1. 创建产品

1. 登录 Tuya IoT 平台
2. 进入 **产品** > **产品开发** > **创建产品**
3. 选择合适的产品品类与方案
4. 填写产品名称，获得 **产品 PID**（`product_key`）

:::caution 方案必须支持 AI 功能
只有支持 AI 功能的产品方案，开发页面里才有 **产品 AI 功能** 入口（这类产品带 AI 标识）。方案选错了，第 3 步无处可配——这不是配置漏了，得换方案或品类。
:::

### 2. 配置 AI Agent

1. 在产品页面找到 **AI 配置** 或 **智能体管理**
2. 创建或绑定一个 AI Agent
3. 配置 Agent 的基础参数：
   - 系统提示词（System Prompt）
   - TTS 语音类型
   - 语言设置

详细步骤参考 Tuya 官方文档：[创建 Agent](https://developer.tuya.com/cn/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud)

### 3. 把智能体投放到产品（设备直连）

设备直连对应官方的「投放到设备进行直连」：产品与智能体绑定后，基于该产品信息授权的设备激活后自动调用该智能体。三个投放入口：

- **我的智能体** > **更多操作** > **智能体投放**
- **我的智能体** > **Agent 管理** > **智能体投放**
- **产品开发** > **功能定义** > **产品 AI 功能** 下快捷投放

从产品侧走完整流程：**产品** > **产品开发** → 选中产品 → **继续开发** → **01 功能定义** > **产品 AI 功能** → 卡片 **新增智能体**，三种添加方式：

| 方式 | 说明 |
|------|------|
| 根据功能推荐智能体 | 先选 AI 功能，平台再推荐对应的智能体模板 |
| 选择涂鸦智能体应用 | 涂鸦官方应用（AI 宠物、AI 能源等），免开发，需该应用已投放到本产品方案下 |
| 选择账号下已有智能体 | 账号下自建的智能体，**状态须为「已上架」** |

添加完设备端智能体后，可以单击 **音频配置** 选择音频格式与音色。设备端另有一套声明——`session_attrs_json` 里的 `tts.order.supports`，见[配置音频格式](./audio-format)。

设备端智能体**一个产品只能投一个**：官方表格里设备端的「多个智能体投放」写的是「暂不支持，多智能体规划中」，支持多个的是面板端。

:::note 授权码要带「AI 智能体接入」标记
采购模组或提货授权码**之前**就要把智能体绑定到产品，这样生成的授权码才会同时带上 **AI 智能体接入** 授权。未携带该标记的设备会被识别为普通设备，产生的 AI token 消耗不享受基础减免。详见[智能体投放及费用](https://developer.tuya.com/cn/docs/iot/agent-deploy?id=Kfnx3351272vh)。
:::

### 4. 配置工作流（可选）

如需实现图片理解、结构化输出等高级功能，需要配置**工作流**。详见[创建工作流](./create-workflow)。

### 5. 获取授权码

1. 在产品页面申请测试用授权码（uuid + authkey），详见[领取授权码](../get-authkey)
2. 大规模出货需联系 Tuya 商务购买授权码

### 6. 在代码中使用

**RTC TCP Client：**

```c
tai_config_t cfg = {
    // ...
    .agent_token = NULL,  // NULL 使用产品默认 Agent
    // 如有多个 Agent，可指定特定 agent_token
};
```

**RTC Client：** Agent 的选择由 `session_token` 获取时的产品配置决定，无需在 SDK 侧指定。

## Agent Token

如果产品绑定了多个 Agent（例如不同场景），可通过 `agent_token` 字段切换：

```c
tai_config_t cfg = {
    .agent_token = "specific_agent_token_here",
    // ...
};
```

`agent_token` 从 Tuya IoT 平台的 Agent 管理页面获取。

## 注意事项

- 每个产品可以绑定一个默认 Agent，设备不指定 `agent_token` 时使用默认 Agent
- 下行 TTS 音频格式由设备端通过 `session_attrs_json` 中的 `tts.order.supports` 声明，详见[配置音频格式](./audio-format)
- 工作流配置修改后立即生效，无需重新连接
- 测试授权码有使用数量和时间限制
- 想让智能体在设备上报某个 DP 时**主动**推消息，还要配设备事件规则和触发器，见[智能体触发器](../tutorials/agent-trigger)
