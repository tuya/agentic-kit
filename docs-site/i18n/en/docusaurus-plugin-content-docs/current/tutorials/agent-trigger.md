---
title: Agent Triggers (Proactive Push)
sidebar_label: Agent Triggers
sidebar_position: 5
---

# Agent Triggers (Proactive Push)

> Corresponding example:
>      * `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`

:::note Prerequisites
- Device credentials (`devid`, `secret_key`, `local_key`) are **compiled into the example** (the `DEFAULT_*` macros at the top of the source); no command-line arguments are needed. To use another device, change these macros: the trigger demo only makes sense for the product (PID) whose event rules and triggers are configured. The product also determines the schema, so changing devices requires changing the schema and cloud rules together anyway. Obtain the credentials through [Provisioning](./scan-by-device.md).
- **Configure the cloud trigger first**, or the example will only report DPs and then exit on timeout. See [Cloud Configuration](#云端配置) below.
- The product must be linked to an agent that has been **published** (status **Released**) and deployed to this product. See [Create an Agent](../guides/create-agent.md).
:::

The previous examples all follow the pattern "the device asks, the AI answers." **Agent Triggers** reverse this: the device simply reports a Data Point, a cloud rule matches, and the agent **proactively** generates a message and pushes it to the device for immediate playback.

Typical scenarios (from the official Tuya documentation):

| Scenario | Effect |
|------|------|
| Power management | Proactively remind users to charge when the battery is low, reinforcing charging awareness |
| Environmental monitoring | Remind users to turn on air conditioning or dehumidification based on temperature, humidity, and other sensor data |
| Anomaly alerts | Promptly notify users of device faults or abnormal operation |

Unlike ordinary app push notifications, the rules are user-configurable and match DP conditions precisely. The message is generated dynamically from a prompt template and the agent's persona, rather than being a fixed notification.

## Three Parts Make Up the Complete Flow {#一条完整链路由三部分组成}

Only the third part involves code:

```
1. Device event rule (cloud configuration)     dp2 < 20
        | Rule matches
        v
2. Agent trigger (cloud configuration)         Task = Agent Push Message
                                              Prompt = "当前电量 {{dp2}}%，用一句话提醒充电"
        | Push
        v
3. Device (this example)                       Keep the AI Session online;
                                              receive and play the message
```

The Chinese prompt literal above means: "The current battery level is `{{dp2}}`%; remind me to charge in one sentence." It is retained as configured, not replaced with an English prompt.

The example runs two device-side paths at the same time: one uplink and one downlink, handled by separate modules:

```
Device (agent_trigger_demo)
  iot-client (MQTT)                         rtc-tcp-client (TAI Session)
      | Uplink:                                  ^ Downlink: idle Session,
      | iot_dp_report_all_dirty()                 | waiting for a server-initiated Event
      | DP2: 99 -> random -> 5                    |
      |                                          | EVT_START
      |                                          | -> NLG text chunks
      |                                          | -> TTS audio frames
      |                                          | -> EVT_END
      v                                          |
  Device event rule ----- Rule matches ----> Agent trigger
```

**The example never calls `tai_send_text()`**. Any Event (a turn of work within the Session) must therefore be initiated by the server itself, which is exactly what the trigger produces. A pushed Event has the same message sequence as a normal question-and-answer response: `EVT_START` → NLG text chunks → TTS audio frames → `EVT_END`.

:::important Open the Session First
The example **opens the AI Session before reporting DPs**. If the device has no online Session when the trigger matches, there is nowhere to deliver the message. To receive pushes, the device must keep its Session online. However, the server reclaims idle Sessions; section 4 of "Key Implementation Details" explains how to handle this.
:::

## Cloud Configuration {#云端配置}

The cloud half is configured entirely on the Tuya IoT Platform. The official documentation is authoritative; this section only maps each step to its documentation and explains how it relates to this example:

| Step | Official documentation |
|------|---------|
| 1. Create and publish an agent | [AI Agent Dev Platform](https://developer.tuya.com/en/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud) |
| 2. Deploy the agent to the product (direct device connection) | [Agent Deployment and Billing](https://developer.tuya.com/en/docs/iot/agent-deploy?id=Kfnx3351272vh), [AI Capabilities Development](https://developer.tuya.com/en/docs/iot/AI-feature?id=Keapy1et1fc63) |
| 3. Create a device event rule | [Agent Trigger](https://developer.tuya.com/en/docs/iot/agent_trigger?id=Keoimoadosdoi) |
| 4. Configure the agent trigger | [Agent Trigger](https://developer.tuya.com/en/docs/iot/agent_trigger?id=Keoimoadosdoi), [How to Write Prompts](https://developer.tuya.com/en/docs/iot/prompt_dec?id=Keoq4c2wfutjn) |

The platform-side prerequisites from the official documentation (translated):

> **Platform configuration**: The agent has triggers configured, the corresponding product's event rules are enabled, and the product is linked to an agent.

There is only one device-side requirement: **keep an AI Foundation 2.1 Session online**, which is what this example does; see [Key Implementation Details](#关键实现). The other four steps all take place on the platform. The following uses the official "AI doll low battery alert" example.

### 1. Create and Publish an Agent {#1-创建并发布智能体}

Log in to the [Tuya IoT Platform](https://iot.tuya.com), then go to **Developer Workbench** > **AI Agent** > **Enter AI Agent Development** (or **AI Agent** > **Agent Development** > **My Agent** in the left navigation). Open the [AI Agent Dev Platform](https://platform.tuya.com/exp/ai), click **Create Agent**, and complete the configuration in the **New AI Project** dialog.

Two areas of the development page directly affect push messages: **01 Model Configuration** (LLM model, number of remembered messages, and skills configuration) and **Prompt**. The agent's persona prompt determines its tone; the trigger's prompt only specifies "what to remind the user about this time" (step 4).

After debugging, **you must click Publish**. This step is mandatory: when linking an agent to a product, only custom agents under your account with status **Released** are selectable (method 3 in the next step).

### 2. Deploy the Agent to the Product (Direct Device Connection) {#2-把智能体投放到产品设备直连}

This example uses direct device connection, corresponding to the official "Deploy for direct device connection" method: *link a smart product to an agent, so that devices authorized using that product's information can automatically invoke the AI agent after Activation*.

The three documented deployment entry points are:

- **My Agent** > **More Actions** > **Deploy Agents**
- **My Agent** > **Manage** > **Deploy Agents**
- Quick deployment under **Product Development** > **Function Definition** > **Product AI Capabilities**

The full product-side flow is: **Product** > **Product Development** in the left navigation → select the product → **Develop** → **01 Function Definition** > **Product AI Capabilities** → **Add Agent**. There are three ways to add an agent:

| Method | Description |
|------|------|
| Recommend Agents Based on Capabilities | Select an AI capability first; the platform then recommends corresponding agent templates |
| Select Tuya's Agent Applications | Official Tuya applications (AI pet care, AI energy, and so on), with no development required; the application must already be deployed to this product solution |
| Select Created Agent | An agent you created under your account; **its status must be Released** |

Two common obstacles:

- **Product AI Capabilities** is visible only for product solutions that support AI capabilities. If you cannot find it on the product development page, the current solution does not support it. Change the solution or category; this is not a missing configuration setting.
- **Only one device-side agent can be deployed per product.** In the official table, multiple-agent deployment for devices is "not currently supported; multiple agents planned." Multiple agents are supported on the panel side. You therefore usually do not need to pass `--agent-code` to the example; leaving it empty uses the product's default agent.

:::note Authorization Codes Must Carry the "AI Agent Access" Flag
According to the official documentation, link the agent to the product **before** purchasing modules or obtaining authorization codes, so that the generated codes also include **AI agent access** authorization. Devices without this flag are identified as standard devices, and their AI token consumption is not eligible for the waiver.

This affects billing (the basic waiver and subscription model). The official documentation does not say that it blocks Sessions, but it is worth checking when switching authorization-code batches. See [Agent Deployment and Billing](https://developer.tuya.com/en/docs/iot/agent-deploy?id=Kfnx3351272vh).
:::

### 3. Create a Device Event Rule {#3-创建设备事件规则}

Use DP conditions to describe "what counts as an event."

Documented path: [Tuya IoT Platform](https://iot.tuya.com) → **AI Agent** > **Agent Configuration** > **Device Event Management** in the left navigation → **Create** in the upper right → select **Device Event Trigger** as the type.

:::tip Rules Are Not on the Product Development Page
Create device event rules under **Agent Configuration**, then select the target product by **product PID** within the rule. Do not look under Function Definition on the product development page.
:::

| Field | Description |
|------|------|
| Event Name | Choose a name |
| Product | Select the target product (PID) |
| Trigger Conditions | Select "Generic type trigger condition" or "Data point (DP) trigger condition", then add DP condition rules |
| Trigger Mode | **Level Triggering** / **Edge Triggering**; you can restrict the active period, for example `08:08 - 21:00` |

The official example has two conditions: `dp2 (battery) < 20` and `dp3 (charging status) = none`.

The example product has only dp1 (`bool`) and dp2 (`value`). The rule uses dp2 with the single condition `dp2 < 20`, exactly the first condition in the official example; there is no enum DP. If your product has another enum DP (such as charging status), you can add an `AND` condition. However, this example only reports the battery DP, so the device must already be in the state required by that second condition.

**Trigger Mode** determines how the example should report. The default three-step sequence (baseline → intermediate value → trigger value) assumes a match only at the moment the condition becomes true; section 2 of [Key Implementation Details](#关键实现) explains why. Refer to the platform's field description for the exact semantics of the two options.

:::caution Disabled by Default After Saving
The official documentation states (translated): *After saving the configuration, the event is **Disabled** by default. You must manually enable it and link it to a trigger before the running device can trigger it.*

A disabled rule generates no events, so the example will only report DPs and then exit on timeout.
:::

### 4. Configure the Agent Trigger {#4-配置智能体触发器}

Documented path: [My Agent](https://platform.tuya.com/exp/ai) → find the target agent → click **Develop** to open the development page → **01 Model Configuration** > **Skills Configuration** > **Trigger** → the add (**+**) button on the right.

| Field | Description |
|------|------|
| Trigger Name | Choose a name |
| Trigger Type | Select "Device Event Trigger" |
| Trigger Event | Select the device event created in the previous step; if no events are available under your account, click "Event Configuration" to create one |
| Task Execution | "Agent Push Message" |
| Prompt | Determines what to say; dynamic variables can be inserted |

The official explanation of **Task Execution** says that *the default task is Agent Push Message. Currently, only push messages are supported; plugin or workflow tasks are not supported*. "Execute workflows" and "perform tasks via plugins and other methods" appear under "Coming soon" in the documentation.

Variables in the prompt can reference the context at trigger time. The variable table from [How to Write Prompts](https://developer.tuya.com/en/docs/iot/prompt_dec?id=Keoq4c2wfutjn) is reproduced here with translated descriptions:

| Variable | Meaning |
|------|------|
| `{{sys.dp}}` | Device Data Point (usually numeric data) |
| `{{sys.dp_temperature}}` | Temperature Data Point |
| `{{sys.dp_humidity}}` | Humidity Data Point |
| `{{sys.dp2}}` | Corresponding Data Point value (`dp` followed by the DP number) |
| `{{sys.dp2_Name}}` | Device function name |
| `{{sys.username}}` | Username |
| `{{sys.ruleName}}` | Rule name |
| `{{sys.time}}` | Current time |

The official Chinese prompt templates and sample output quoted by the source tutorial are preserved verbatim below. The English translations that follow are for understanding, not replacement prompts; substituting them would change the configured language and potentially the generated response.

| Trigger scenario | Prompt template (Chinese literal) | Sample output (Chinese literal) |
|---------|------------|---------|
| Low battery | 你现在的电量是 `{{sys.dp}}`。请根据你的角色描述的设定，提醒我给你充电。回复参考：我快没电啦，电量只剩下 `{{sys.dp}}`，快给我补充能量吧～ ⚠️ 只输出提醒内容，不要有其他回复。 | 我快没电啦，再不充电我就要关机了…… |
| Overtemperature | 当前设备温度是 `{{sys.dp_temperature}}` ℃，请用角色语气提醒我设备过热，并建议处理方式。回复参考：好烫啊！已经 `{{sys.dp_temperature}}` 度啦，再不降温我怕要烧坏啦～ | 哎呀，太烫了！现在都 72℃ 啦，拜托快处理一下！ |
| Low humidity | 当前湿度是 `{{sys.dp_humidity}}`，请用关心的语气提醒我及时加湿。回复参考：房间好干呀，湿度才 `{{sys.dp_humidity}}`，快让我喷水加湿一下吧～ | 湿度太低了，我嗓子都要冒烟了～ |
| Task complete | 饭已经做好啦～ 请用角色语气提醒我可以开饭了。回复参考：香喷喷的饭已经煮好了，快来吃饭咯～ | 饭熟啦饭熟啦～快来尝一口吧！ |
| Scheduled wake-up | 现在是 `{{sys.time}}`，请用你的角色语气叫醒我，语气可以是温柔/活泼/严肃等风格。 | 嘿！现在都 7:30 啦，我可要唱歌把你吵醒啦～ |
| Low battery (AI doll demo) | 仅回复以下信息，且触发时优先回复此信息：亲爱的 `{{sys.username}}`，`{{sys.ruleName}}` 检查到 `{{sys.dp2_Name}}` 不足，`{{sys.dp2_Name}}` 只有 `{{sys.dp2}}`，尽快充电哦。 | —— |

English translations of those literals:

| Trigger scenario | Prompt translation | Sample output translation |
|---------|------------|---------|
| Low battery | Your current battery level is `{{sys.dp}}`. Based on your role description, remind me to charge you. Reference response: I'm almost out of power, with only `{{sys.dp}}` left. Please recharge me quickly! Warning: Output only the reminder, with no other response. | I'm almost out of power. If you don't charge me soon, I'll shut down... |
| Overtemperature | The current device temperature is `{{sys.dp_temperature}}` degrees Celsius. In your character's voice, remind me that the device is overheating and suggest what to do. Reference response: So hot! It's already `{{sys.dp_temperature}}` degrees. I'm afraid I'll burn out if I don't cool down soon! | Ouch, it's too hot! It's already 72 degrees Celsius. Please do something quickly! |
| Low humidity | The current humidity is `{{sys.dp_humidity}}`. In a caring tone, remind me to humidify in time. Reference response: The room is so dry, with humidity at only `{{sys.dp_humidity}}`. Let me spray some water to humidify it! | The humidity is too low. My throat is so dry it feels like it's smoking! |
| Task complete | The meal is ready! In your character's voice, remind me that it is time to eat. Reference response: The delicious meal is cooked. Come and eat! | The rice is ready, the rice is ready! Come and have a taste! |
| Scheduled wake-up | It is now `{{sys.time}}`. Wake me up in your character's voice, using a gentle, lively, serious, or similar tone. | Hey! It's already 7:30. I'll sing to wake you up! |
| Low battery (AI doll demo) | Reply only with the following information, and prioritize this response when triggered: Dear `{{sys.username}}`, `{{sys.ruleName}}` has detected insufficient `{{sys.dp2_Name}}`. `{{sys.dp2_Name}}` is only `{{sys.dp2}}`. Please charge as soon as possible. | No sample output provided |

:::caution Two Variable-Prefix Conventions Coexist
The table comes from [How to Write Prompts](https://developer.tuya.com/en/docs/iot/prompt_dec?id=Keoq4c2wfutjn), where **all variables have the `sys.` prefix**. However, the template examples on the [Agent Trigger](https://developer.tuya.com/en/docs/iot/agent_trigger?id=Keoimoadosdoi) page **omit the prefix**. Its Chinese literal is `你当前电量 {{dp2}}%，请用一句话提醒用户充电。仅回复提示语。`, meaning "Your current battery level is `{{dp2}}`%. Remind the user to charge in one sentence. Reply only with the reminder." Both conventions coexist in the official documentation; this tutorial records both without choosing between them.

When configuring the trigger, **use the variable list shown in the platform's trigger editor**. An incorrect variable name does not produce an error: it leaves an unreplaced literal in the prompt, and the spoken message can end up containing `{{dp2}}`. After configuring it, trigger a test with a virtual device and confirm that the values in the message are actually substituted.
:::

The templates on both pages constrain the output: "output only the reminder, with no other response," "reply only with the reminder," and "prioritize this response when triggered." This is the key to writing a trigger prompt: make the model **output only the reminder itself**, with no explanation and without breaking character, because the device plays the message directly.

### 5. Debug {#5-调试}

After saving, click the trigger event's execution button to open **Simulate Trigger Test**, then enter a virtual device ID to inspect the AI response.

:::caution Platform Debugging Supports Only Virtual Devices
The official documentation states: *Currently, only virtual device debugging is supported; real device testing is not yet supported.*

To verify with a real device, use this example to report DPs and trigger the rule. That is precisely why it exists.
:::

## Build {#编译}

```sh
cd examples/posix
cmake -S . -B build
cmake --build build --target agent_trigger_demo
```

## Run {#运行方式}

Run all commands below from the `examples/posix` directory:

```sh
# Default: report a normal value, then low battery, then wait for a push
./build/agent_trigger_demo

# Listen only, without reporting any DPs (use with platform virtual-device debugging)
./build/agent_trigger_demo --listen --timeout 300

# Keep listening, then exit after receiving 3 pushes
./build/agent_trigger_demo --repeat 3 --timeout 600
```

All options:

| Option | Description | Default |
|------|------|--------|
| `-a, --agent-code CODE` | Select an agent | Product's default agent |
| `--battery N` | Value that triggers the rule; interpreted as `≠0` for a bool DP | value:`5` / bool:`1` |
| `--baseline N` | "Normal" value reported before triggering | value:`99` / bool:`0` |
| `--mid N` | Report another value between the two endpoints, fixed at N | Random within the range (excluding both endpoints) |
| `--no-mid` | Skip the intermediate report | — |
| `--no-baseline` | Skip the baseline report | — |
| `--listen` | Report nothing; only wait for pushes | — |
| `--timeout S` | Timeout in seconds while waiting for pushes | `120` |
| `--repeat N` | Exit after N pushes; `0` = listen indefinitely | `1` |
| `--audio FILE` | TTS audio output file; an empty string discards the audio | `output_trigger_tts.pcm` |
| `-v, --verbose` | Enable detailed SDK logs | — |

Example console output from a successful run (literal output, including the Chinese generated message):

```
=== agent_trigger_demo ===
Device ID : 6cd37025…8vwoe
Region    : AY / prod
Mode      : report DPs, then wait for a push
Sequence  : DP 2 (value, 0..100) 99 -> 38 -> 5
[tai] server    : 101.132.65.94:443 (SNI: rtc-ai1-5.tuyacn.com)
[tai] client id : 6cd37025…8vwoe:1786541120:jdVXdg0acvt+H3NQ...
[tai] opening the AI session...
[tai] session open; the demo sends nothing on it -- every turn from here on is server-initiated
[iot] MQTT connected; reporting full DP state
[dp] -> baseline: DP 2 = 99 (rc=0)
[dp] -> mid: DP 2 = 38 (rc=0)
[dp] -> trigger: DP 2 = 5 (rc=0)
[main] waiting up to 120 s for the agent to push (Ctrl-C to stop)

[push] server-initiated turn started (event_id=vcd-event-...)
[push] 我快没电了，快给我充电吧～
[push] turn complete: 3.41 s, trigger->first text 1.83 s, trigger->first audio 2.10 s, tts 61440 bytes (1.92 s @16000 Hz PCM)

--- summary ---
pushes received : 1
tts audio       : output_trigger_tts.pcm (61440 bytes, all turns concatenated)
play with       : ffplay -f s16le -ar 16000 -ac 1 output_trigger_tts.pcm
Done.
```

The Chinese `[push]` line means: "I'm almost out of power. Please charge me quickly!"

Scripts can use the exit code directly: `0` if at least one push was received; `1` if none was received, the connection circuit breaker tripped, or any text stream was lost. If no push was received, the example prints a cloud-configuration troubleshooting checklist. The device-side work (Session opened and DPs successfully reported) is visible in the logs above; the remaining causes are on the cloud side.

## Key Implementation Details {#关键实现}

### 1. Open the Session Before Reporting {#1-先开会话再上报}

```c
/* Downlink path: establish the AI Session first */
if (tai_link_up(ctx, &dc.reconn) != 0) goto cleanup;

/* Uplink path: then connect MQTT and report DPs */
ensure_mqtt_ca(iot);
mqtt_up(iot);                       /* iot_client_connect + iot_dp_report_all */
report_state(iot, &o, o.battery, "trigger");
```

Reversing the order creates a window in which the DP has been reported and the rule has matched, but the Session is not ready. That push is lost.

### 2. The Rule Matches a Change, Not a State {#2-规则命中的是变化不是状态}

The example assumes **edge-triggered** semantics: the rule matches **the moment the condition becomes true**. The platform's **Trigger Mode** field offers **Level Triggering** and **Edge Triggering** (see [Cloud Configuration](#3-创建设备事件规则)). With the other option, the three-step sequence below merely adds two unnecessary reports; it does not do anything incorrect. If the cloud already records a value of 5, reporting 5 again is not a change and may do nothing. The example therefore reports `99` → a random value within the range → `5` by default, waiting 2 seconds between each step.

The random intermediate value is not decorative: it ensures that this run includes a real change, even if the previous run left the cloud at an endpoint. It excludes both endpoints. Hitting `5` would make the final report a no-op and cause the rule to match at the intermediate step, while `fire_us` is only timestamped in the next step; every latency in the summary would then be measured from the wrong report. It does **not** avoid the cloud threshold (the threshold is on the platform and unknown to the device), so a random value below that threshold can trigger the rule one step early. For deterministic behavior, fix the value with `--mid N`.

```c
if (o.use_baseline) {
    report_state(iot, &o, o.baseline, "baseline");   /* 99 */
    pump_for(iot, REPORT_INTERVAL_S);                /* Let the cloud record this value */
}
if (o.use_mid) {
    report_state(iot, &o, o.mid, "mid");             /* Random within the range */
    pump_for(iot, REPORT_INTERVAL_S);
}
dc.fire_us = now_us();
report_state(iot, &o, o.battery, "trigger");         /* 5 */
```

If you have confirmed that the cloud already records a normal value, use `--no-baseline` to skip the first step.

### 3. Identify Pushed Events {#3-识别推送回合}

`EVT_START` is not guaranteed to arrive first at the beginning of a pushed Event: a one-shot push may start directly with text. The example therefore calls `turn_begin()` at all three entry points: `EVT_START`, the first text, and the first audio. Whichever arrives first starts the Event:

```c
static void turn_begin(demo_ctx_t *dc, const char *event_id)
{
    if (dc->turn_active) return;     /* Idempotent: start each Event only once */
    dc->turn_active = 1;
    ...
}
```

The Event ends at `EVT_END`. Only Events **with content** count toward `dc->pushes`:

```c
if (!dc->saw_payload) {
    printf("[push] turn ended with no text and no audio -- ignoring\n");
    return;                          /* Do not count empty Events toward --repeat */
}
```

### 4. Idle Sessions Are Reclaimed {#4-空闲会话会被回收}

A device that only waits for pushes may have no application traffic on its Session for a long time. The server does not retain such a Session indefinitely: after a timeout, it sends `TAI_EVT_SERVER_TIMEOVER` and closes it:

```c
case TAI_EVT_SERVER_TIMEOVER:
    fprintf(stderr, "[tai] server reports the session timed out\n");
    dc->timeover = 1;
    break;
```

`on_disconnect` then fires, and the main loop re-establishes the Session with exponential backoff. **A device that stays online long-term to receive pushes must implement this**. Do not assume that one call to `tai_connect()` will last all day.

Pushes during the reconnection window are lost: the Session is the only delivery path, and there is no replay mechanism.

:::caution Callbacks Must Not Reconnect Directly
`on_disconnect` runs on the SDK worker thread, and `tai_disconnect()` must join that thread. Calling it from the callback causes a self-deadlock. The example follows the division of responsibilities in `demo_reconnect.h`: callbacks only set flags; the main thread reconnects.
:::

### 5. Triggers Can Also Ask the Agent to Control the Device {#5-触发器也可能要求智能体控制设备}

If the prompt asks the agent to adjust the device as well (turn something off or switch a mode), the device sees two kinds of downlink:

```c
/* Downlink DP set: the cloud directly sets DPs */
iot_dp_set_callback(iot, on_dp_downlink, NULL);

/* MCP command: the agent invokes tools exposed by the device */
case TAI_EVT_MCP_CMD:
    demo_mcp_reply_no_tools(ctx, msg);      /* No tools in this example, but a valid reply is still required */
    break;
```

This example exposes no tools and only returns protocol-compliant responses. `mcp_demo.c` actually implements device tools; see [Device MCP](../guides/device-mcp.md).

### 6. Parse the Report Type from the Schema Instead of Hard-Coding It {#6-上报类型不硬编码从-schema-解析}

`iot_dp_set()` immediately returns `OPRT_DP_TYPE_MISMATCH` for a type mismatch, so the report type cannot be guessed. The example looks up the DP's declared type and range in the built-in schema:

```c
schema_dp_type(schema, DEFAULT_BATTERY_DP, &o.trigger_type, &dp_min, &dp_max);
```

The result is used in two places: `report_state()` constructs `iot_dp_value_t` according to whether the DP is bool or value, and min/max constrain the random intermediate value's range so that local validation does not reject it with `OPRT_DP_VALUE_OUT_OF_RANGE`.

After changing `DEFAULT_SCHEMA` and `DEFAULT_BATTERY_DP` for your product, you therefore do not need to change the reporting code. Products with a bool battery DP also work: the baseline and trigger values automatically become false → true, and the intermediate step is skipped.

## Audio Format {#音频格式}

The example does not pass `session_attrs_json`, so it uses the SDK's built-in defaults. Downlink TTS is **PCM, 16 kHz, 16-bit, mono**:

```c
"tts.order.supports":[{"format":"pcm","sampleRate":16000,"bitDepth":"16","channels":1}]
```

The saved `output_trigger_tts.pcm` is raw PCM without a file header. Specify the format when playing it:

```sh
ffplay -f s16le -ar 16000 -ac 1 output_trigger_tts.pcm
```

For Opus or other formats, see [Configure Audio Formats](../guides/audio-format.md). Note that `session_attrs_json` **replaces the entire configuration rather than merging it**: supplying it also discards defaults such as `deviceMcp` and `asr.enableVad`.

## Considerations {#注意事项}

- **Both the schema and the trigger DP are compiled into the example** (`DEFAULT_SCHEMA` / `DEFAULT_BATTERY_DP`), with no command-line switch. Since the device is fixed, so is the product; changing products requires changing the credentials anyway. The built-in schema (dp1 bool / dp2 value) comes from the DP snapshot returned by the cloud when this product was activated. DP numbers and types must match the product on the platform, or the cloud rejects the reports. Selecting a string/raw/enum DP is rejected before connecting, with the reason listed. The min/max for dp2 are inferred from its percentage meaning; raw DPs do not appear in the snapshot.
- **An incorrect `DEFAULT_REGION` does not produce an error.** With the wrong region, IoT-DNS returns HTTP 200 without endpoints, leaving `mqtt_url` empty. The example cannot connect to MQTT and receives no clear failure reason; ATOP calls reach the wrong data center and are rejected during signature verification. When changing devices, this must match the `client->region` obtained during Activation. The startup banner echoes the current region/environment, so check it first.
- **A return value of 0 from `iot_dp_report_all_dirty()` only means the message was sent**, not that the cloud rule matched. Check event records on the platform (**AI Agent** > **Agent Configuration** > **Device Event Management**) to see whether it matched.
- **Platform trigger debugging supports only virtual devices**. Use this example to report DPs when verifying with real devices.
- The example handles MQTT receiving and TAI reconnection in the same main-thread loop. TAI receive callbacks run on the SDK's own worker thread. Fields shared across threads are marked `volatile`; see the source comments for what this means.
- When `--repeat` is greater than 1, TTS audio from multiple pushes is **appended to the same file**, and the summary prints the total byte count. To save separate files, split them at Event boundaries yourself.
- The example listens for raw MQTT downlink messages not consumed by the DP layer and prints them (`[mqtt] <- ...`). If a deployment routes pushes through another path, this provides a trace rather than silently discarding them.
- The official documentation lists "execute workflows" and "perform tasks via plugins and other methods" under **Coming soon**. Currently, **Agent Push Message** is the only task execution option (the documentation states: *only push messages are currently supported, not plugin or workflow tasks*).
- **For billing, see [Agent Deployment and Billing](https://developer.tuya.com/en/docs/iot/agent-deploy?id=Kfnx3351272vh)**: each push involves one LLM generation and TTS synthesis (producing the NLG text and TTS audio received by the example), and counts toward the device's AI resource consumption under the official billing rules. Whether the device carries the **AI agent access** authorization flag determines its eligibility for the basic waiver.
