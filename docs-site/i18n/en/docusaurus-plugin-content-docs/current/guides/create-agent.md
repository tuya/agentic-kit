---
title: Create and Configure an Agent
sidebar_label: Create an Agent
sidebar_position: 3
---

# Create and Configure an Agent

This guide explains how to create a product, create an AI agent, and **deploy** it to the product on the Tuya IoT Platform so that devices automatically use the agent after Activation. The official documentation is authoritative for platform-side procedures; this guide connects the steps and identifies the SDK interfaces involved:

| Step | Official documentation |
|------|---------|
| Create a product | [Create Products](https://developer.tuya.com/en/docs/iot/create-product?id=K914jp1ijtsfe) |
| Configure an AI agent | [AI Agent Dev Platform](https://developer.tuya.com/en/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud) |
| Deploy the agent to the product | [Agent Deployment and Billing](https://developer.tuya.com/en/docs/iot/agent-deploy?id=Kfnx3351272vh), [AI Capabilities Development](https://developer.tuya.com/en/docs/iot/AI-feature?id=Keapy1et1fc63) |

:::tip Not Started Yet?
If you do not yet have a product PID and device authorization code (uuid + authkey), follow this guide first. These are prerequisites for using Agentic-kit. See the integration flow in the [Introduction](../intro).
:::

## Prerequisites {#前置条件}

- A registered [Tuya IoT Platform](https://iot.tuya.com) account
- An understanding of your product's form factor (speaker, educational camera, robot, and so on)

## Steps {#步骤}

### 1. Create a Product {#1-创建产品}

1. Log in to the Tuya IoT Platform.
2. Go to **Product** > **Product Development** > **Create Product**.
3. Select an appropriate product category and solution.
4. Enter the product name and obtain the **product PID** (`product_key`).

:::caution The Solution Must Support AI Capabilities
Only product solutions that support AI capabilities show **Product AI Capabilities** on the development page (these products have an AI label). If you select the wrong solution, there is nowhere to configure step 3. This is not a missing configuration setting: you must change the solution or category.
:::

### 2. Configure an AI Agent {#2-配置-ai-agent}

1. Find **AI Configuration** or **Agent Management** on the product page.
2. Create or link an AI agent.
3. Configure the agent's basic parameters:
   - System prompt
   - TTS voice type
   - Language settings

For detailed steps, see the official Tuya documentation: [Create an Agent](https://developer.tuya.com/en/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud).

### 3. Deploy the Agent to the Product (Direct Device Connection) {#3-把智能体投放到产品设备直连}

Direct device connection corresponds to the official "Deploy for direct device connection" method: after a product is linked to an agent, devices authorized using that product's information automatically invoke the agent after Activation. There are three deployment entry points:

- **My Agent** > **More Actions** > **Deploy Agents**
- **My Agent** > **Manage** > **Deploy Agents**
- Quick deployment under **Product Development** > **Function Definition** > **Product AI Capabilities**

The full product-side flow is: **Product** > **Product Development** > select the product > **Develop** > **01 Function Definition** > **Product AI Capabilities** > **Add Agent**. There are three ways to add an agent:

| Method | Description |
|------|------|
| Recommend Agents Based on Capabilities | Select an AI capability first; the platform then recommends corresponding agent templates |
| Select Tuya's Agent Applications | Official Tuya applications (AI pet care, AI energy, and so on), with no development required; the application must already be deployed to this product solution |
| Select Created Agent | An agent you created under your account; **its status must be Released** |

After adding the device-side agent, click **Configure Audio** to select the audio format and voice. The device has a separate declaration: `tts.order.supports` in `session_attrs_json`. See [Configure Audio Formats](./audio-format).

**Only one device-side agent can be deployed per product.** The official table describes multiple-agent deployment for devices as "not currently supported; multiple agents planned." Multiple agents are supported on the panel side.

:::note Authorization Codes Must Carry the "AI Agent Access" Flag
Link the agent to the product **before** purchasing modules or obtaining authorization codes, so that the generated codes also include **AI agent access** authorization. Devices without this flag are identified as standard devices, and their AI token consumption is not eligible for the basic waiver. See [Agent Deployment and Billing](https://developer.tuya.com/en/docs/iot/agent-deploy?id=Kfnx3351272vh).
:::

### 4. Configure a Workflow (Optional) {#4-配置工作流可选}

Advanced features such as image understanding and structured output require a **workflow**. See [Create a Workflow](./create-workflow).

### 5. Obtain Authorization Codes {#5-获取授权码}

1. Request test authorization codes (uuid + authkey) on the product page. See [Obtain Authorization Codes](../get-authkey).
2. For large-scale shipments, contact Tuya sales to purchase authorization codes.

### 6. Use the Agent in Code {#6-在代码中使用}

**RTC TCP Client:**

```c
tai_config_t cfg = {
    // ...
    .agent_token = NULL,  // NULL uses the product's default agent
    // If there are multiple agents, specify a particular agent_token
};
```

**RTC Client:** Agent selection is determined by the product configuration when obtaining the `session_token`. You do not need to specify it in the SDK.

## Agent Token {#agent-token}

If a product is linked to multiple agents (for example, for different scenarios), you can switch between them through the `agent_token` field:

```c
tai_config_t cfg = {
    .agent_token = "specific_agent_token_here",
    // ...
};
```

Obtain `agent_token` from the Agent Management page on the Tuya IoT Platform.

## Considerations {#注意事项}

- Each product can have one default agent; devices use it when `agent_token` is not specified.
- The device declares the downlink TTS audio format through `tts.order.supports` in `session_attrs_json`. See [Configure Audio Formats](./audio-format).
- Workflow configuration changes take effect immediately, without reconnecting.
- Test authorization codes have usage-count and time limits.
- To have an agent **proactively** push a message when a device reports a particular DP, you must also configure device event rules and triggers. See [Agent Triggers](../tutorials/agent-trigger).
