---
title: 如何创建和配置 Agent
sidebar_label: 创建 Agent
sidebar_position: 3
---

# 如何创建和配置 Agent

本指南说明如何在 Tuya IoT 平台上创建产品、配置 AI Agent，使设备具备 AI 对话能力。

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
2. 进入 **产品开发** → **创建产品**
3. 选择合适的产品品类
4. 填写产品名称，获得 **产品 PID**（`product_key`）

### 2. 配置 AI Agent

1. 在产品页面找到 **AI 配置** 或 **智能体管理**
2. 创建或绑定一个 AI Agent
3. 配置 Agent 的基础参数：
   - 系统提示词（System Prompt）
   - TTS 语音类型
   - 语言设置

详细步骤参考 Tuya 官方文档：[创建 Agent](https://developer.tuya.com/cn/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud)

### 3. 配置工作流（可选）

如需实现图片理解、结构化输出等高级功能，需要配置**工作流**。详见[创建工作流](./create-workflow)。

### 4. 获取授权码

1. 在产品页面申请测试用授权码（uuid + authkey），详见[领取授权码](../get-authkey)
2. 大规模出货需联系 Tuya 商务购买授权码

### 5. 在代码中使用

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
