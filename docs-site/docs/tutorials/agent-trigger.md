---
title: 智能体触发器（主动推送）
sidebar_label: 智能体触发器
sidebar_position: 5
---

# 智能体触发器（主动推送）

> 对应示例：
>      * `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`

:::note 前置条件
- 设备凭据（`devid`、`secret_key`、`local_key`）—— **编译进示例**（源码顶部的 `DEFAULT_*` 宏），无需命令行传入。换设备请改这几个宏：触发器 demo 只对配好了事件规则和触发器的那个产品（PID）有意义，而产品又决定了 schema，所以换设备本来就得连 schema 和云端规则一起换。凭据由[配网](./scan-by-device.md)获得。
- **云端必须先配置好触发器**，否则示例只会上报 DP 然后超时退出。云端配置步骤见下文[云端配置](#云端配置)。
- 产品已关联智能体，且该智能体已**发布**（状态「已上架」）、已投放到本产品，参见[创建 Agent](../guides/create-agent.md)。
:::

前面几个示例都是"设备问、AI 答"。**智能体触发器**（Agent Trigger）反过来：设备只是上报了一个数据点，云端规则判定命中后，智能体**主动**生成一段话推给设备，设备直接播出来。

典型场景（来自涂鸦官方文档）：

| 场景 | 效果 |
|------|------|
| 电量管理 | 电量过低时主动提醒充电，强化用户充电意识 |
| 环境监控 | 根据温湿度等传感数据提醒用户开空调、除湿 |
| 异常预警 | 及时告知用户设备故障、运行异常 |

和普通 App 推送的区别在于：规则由用户自主配置、精细匹配 DP 条件；文案由 Prompt 模板结合智能体人设动态生成，而不是一句固定的通知。

## 一条完整链路由三部分组成

只有第三部分是代码：

```
① 设备事件规则（云端配置）      dp2 < 20
        ↓ 命中
② 智能体触发器（云端配置）      任务 = 智能体推消息
                                Prompt = "当前电量 {{dp2}}%，用一句话提醒充电"
        ↓ 推送
③ 设备端（本示例）              保持 AI 会话在线，接收并播放这段话
```

示例把端侧的两条通道同时跑起来 —— 一条上行、一条下行，分属两个模块：

```
        ┌──────────────────────── 设备（agent_trigger_demo）─────────────────────┐
        │                                                                        │
        │  iot-client (MQTT)                          rtc-tcp-client (TAI 会话)   │
        │       │  上行：iot_dp_report_all_dirty()          │  下行：空闲会话      │
        │       │  DP2: 99→随机→5                           │  等待服务端开回合   │
        └───────┼───────────────────────────────────────────┼────────────────────┘
                │                                           │
                ▼                                           ▲
        ┌───────────────┐   规则命中    ┌──────────────┐    │  EVT_START
        │  设备事件规则  │ ───────────▶ │ 智能体触发器  │────┘  → NLG 文本分片
        └───────────────┘              └──────────────┘       → TTS 音频帧
                                                              → EVT_END
```

**示例从不调用 `tai_send_text()`**。因此会话上出现的任何回合都必然是服务端自己开的 —— 这正是触发器的产物。推送回合的报文形状和一次普通问答的回复完全一样：`EVT_START` → NLG 文本分片 → TTS 音频帧 → `EVT_END`。

:::important 会话必须先开
示例的执行顺序是**先开 AI 会话，再上报 DP**。触发器命中时如果设备没有在线会话，这条消息就没有落点。设备端要想收到推送，就必须保持会话在线——而空闲会话会被服务端回收，处理办法见下文「关键实现」的第 4 节。
:::

## 云端配置

云端这一半全部在 Tuya IoT 平台上完成，权威步骤以官方文档为准，本节只做「哪一步看哪篇 + 与本示例的对应关系」：

| 步骤 | 官方文档 |
|------|---------|
| 1. 创建并发布智能体 | [智能体开发平台](https://developer.tuya.com/cn/docs/iot/ai-agent-management?id=Kdxr4v7uv4fud) |
| 2. 把智能体投放到产品（设备直连） | [智能体投放及费用](https://developer.tuya.com/cn/docs/iot/agent-deploy?id=Kfnx3351272vh)、[产品 AI 功能开发](https://developer.tuya.com/cn/docs/iot/AI-feature?id=Keapy1et1fc63) |
| 3. 创建设备事件规则 | [智能体触发器](https://developer.tuya.com/cn/docs/iot/agent_trigger?id=Keoimoadosdoi) |
| 4. 配置智能体触发器 | [智能体触发器](https://developer.tuya.com/cn/docs/iot/agent_trigger?id=Keoimoadosdoi)、[如何编写智能体触发器消息 Prompt](https://developer.tuya.com/cn/docs/iot/prompt_dec?id=Keoq4c2wfutjn) |

官方文档给出的平台侧使用条件（原文照录）：

> **平台配置**：智能体已配置触发器，且对应产品的事件规则已启用。产品已关联智能体。

端侧的要求就一条：**保持 AI Foundation 2.1 会话在线**——也就是本示例做的事，见[关键实现](#关键实现)。剩下的四步全在平台上，下面以官方文档的「AI 娃娃电量过低提醒」为例。

### 1. 创建并发布智能体

登录 [Tuya IoT 平台](https://iot.tuya.com)，走 **开发者工作台** > **AI 智能体** > **进入 AI 智能体开发**（或左侧 **智能体** > **智能体开发** > **我的智能体**），进入[智能体开发平台](https://platform.tuya.com/exp/ai)，单击 **创建 Agent**，在 **新增 AI 项目** 窗口完成配置。

开发页面里和推送文案直接相关的是两处：**01 模型能力配置**（LLM 模型、记忆消息数、技能配置）和 **Prompt**。分工是——智能体的人设 Prompt 决定说话的语气，触发器的 Prompt 只负责说「这次要提醒什么」（第 4 步）。

调试完成后**必须单击「发布」**。这一步不能省：产品关联智能体时，账号下的自定义智能体只有状态为 **已上架** 的才可选（见下一步的方式 3）。

### 2. 把智能体投放到产品（设备直连）

本示例是设备直连，对应官方的「投放到设备进行直连」：*将智能产品与智能体绑定，基于该产品信息授权的设备激活后可自动调用该 AI 智能体进行交互*。

三个投放入口（照录）：

- **我的智能体** > **更多操作** > **智能体投放**
- **我的智能体** > **Agent 管理** > **智能体投放**
- **产品开发** > **功能定义** > **产品 AI 功能** 下快捷投放

从产品侧走完整流程：左侧 **产品** > **产品开发** → 选中产品 → **继续开发** → **01 功能定义** > **产品 AI 功能** → 卡片 **新增智能体**，三种添加方式：

| 方式 | 说明 |
|------|------|
| 根据功能推荐智能体 | 先选 AI 功能，平台再推荐对应的智能体模板 |
| 选择涂鸦智能体应用 | 涂鸦官方应用（AI 宠物、AI 能源等），免开发，需该应用已投放到本产品方案下 |
| 选择账号下已有智能体 | 自建智能体，**状态须为「已上架」** |

两个容易卡住的地方：

- **产品 AI 功能** 只在支持 AI 功能的产品方案下可见。产品开发页面找不到这个入口，说明当前方案不支持，得换方案/品类，不是配置漏了。
- 设备端智能体**一个产品只能投一个**：官方表格里设备端的「多个智能体投放」写的是「暂不支持，多智能体规划中」，支持多个的是面板端。所以示例的 `--agent-code` 一般不用传，留空即用产品默认智能体。

:::note 授权码要带「AI 智能体接入」标记
官方文档：采购模组或提货授权码**之前**就要把智能体绑定到产品，这样生成的授权码才会同时带上 **AI 智能体接入** 授权；未携带该标记的设备会被识别为普通设备，产生的 AI token 消耗不享受减免。

这条影响的是计费口径（基础减免、订阅模式），官方文档没有说它会挡住会话；但换授权码批次时值得先确认一眼，详见[智能体投放及费用](https://developer.tuya.com/cn/docs/iot/agent-deploy?id=Kfnx3351272vh)。
:::

### 3. 创建设备事件规则

用 DP 条件描述「什么情况算一次事件」。

路径（照录）：[Tuya IoT 平台](https://iot.tuya.com) → 左侧导航栏 **智能体** > **智能体配置** > **设备事件管理** → 右上角 **创建** → 类型选 **设备事件触发**。

:::tip 规则不在产品开发页面下
设备事件规则建在**智能体配置**里，在规则里按 **产品 PID** 选目标产品——不要去产品开发的功能定义下找。
:::

| 字段 | 说明 |
|------|------|
| 事件名称 | 自定义 |
| 产品 | 选择目标产品（PID） |
| 触发条件 | 选「通用类型的触发条件」或「功能点触发条件」，添加 DP 条件规则 |
| 触发方式 | **连续触发** / **边缘触发**；可限制生效时段，例如 `08:08 - 21:00` |

官方示例是两个条件：`dp2（电量） < 20` 且 `dp3（充电状态） = none`。

示例产品只有 dp1(bool) 和 dp2(value)，规则用 dp2，即单条件 `dp2 < 20`——正好是官方示例的第一个条件；没有 enum DP。产品若另有 enum DP（例如充电状态），规则里可以再加一条 `且` 条件，但本示例只上报电量这一个 DP，第二个条件得靠设备本身就处在那个状态。

**触发方式**决定示例要怎么上报：本示例默认的三步上报（基线 → 中间值 → 触发值）是按「只在进入条件那一刻命中」写的，理由见下文[关键实现](#关键实现)的第 2 节。两个选项的准确语义以平台该字段的说明为准。

:::caution 保存后默认未启用
原文：*保存配置后，默认事件状态为**未启用**。请务必手动启用事件，并在触发器中关联，设备运行时才可触发。*

未启用的规则不产生事件，示例就只会上报 DP 然后超时退出。
:::

### 4. 配置智能体触发器

路径（照录）：[我的智能体](https://platform.tuya.com/exp/ai) → 找到目标智能体 → 单击 **开发版本** 进入开发页面 → **01 模型能力配置** > **技能配置** > **触发器** → 右侧的添加（**+**）按钮。

| 字段 | 说明 |
|------|------|
| 触发器名称 | 自定义 |
| 触发器类型 | 选择「设备事件触发」 |
| 触发事件 | 选择上一步创建的设备事件；账号下没有可用事件时，单击「事件配置」去创建 |
| 任务执行 | 「智能体推消息」 |
| 提示内容（Prompt） | 决定推什么话，可插入动态变量 |

官方对「任务执行」的说明是*默认任务为智能体推消息。当前仅支持提示消息，不支持插件或工作流任务*；「执行工作流」和「调用插件等形式的任务执行」列在文档的「即将支持」里。

Prompt 里可以插入变量引用触发时的上下文，[Prompt 写法页](https://developer.tuya.com/cn/docs/iot/prompt_dec?id=Keoq4c2wfutjn)给出的变量表（照录）：

| 变量 | 含义 |
|------|------|
| `{{sys.dp}}` | 设备数据点（通常为数值型数据） |
| `{{sys.dp_temperature}}` | 温度数据点 |
| `{{sys.dp_humidity}}` | 湿度数据点 |
| `{{sys.dp2}}` | 相应的数据点值（`dp` 后跟 DP 编号） |
| `{{sys.dp2_Name}}` | 设备功能名称 |
| `{{sys.username}}` | 用户名 |
| `{{sys.ruleName}}` | 规则名称 |
| `{{sys.time}}` | 当前时间 |

官方给出的 Prompt 模板（原文照录）：

| 触发场景 | Prompt 模板 | 生成效果 |
|---------|------------|---------|
| 电量不足 | 你现在的电量是 `{{sys.dp}}`。请根据你的角色描述的设定，提醒我给你充电。回复参考：我快没电啦，电量只剩下 `{{sys.dp}}`，快给我补充能量吧～ ⚠️ 只输出提醒内容，不要有其他回复。 | 我快没电啦，再不充电我就要关机了…… |
| 温度过高 | 当前设备温度是 `{{sys.dp_temperature}}` ℃，请用角色语气提醒我设备过热，并建议处理方式。回复参考：好烫啊！已经 `{{sys.dp_temperature}}` 度啦，再不降温我怕要烧坏啦～ | 哎呀，太烫了！现在都 72℃ 啦，拜托快处理一下！ |
| 湿度过低 | 当前湿度是 `{{sys.dp_humidity}}`，请用关心的语气提醒我及时加湿。回复参考：房间好干呀，湿度才 `{{sys.dp_humidity}}`，快让我喷水加湿一下吧～ | 湿度太低了，我嗓子都要冒烟了～ |
| 任务完成 | 饭已经做好啦～ 请用角色语气提醒我可以开饭了。回复参考：香喷喷的饭已经煮好了，快来吃饭咯～ | 饭熟啦饭熟啦～快来尝一口吧！ |
| 定时唤醒 | 现在是 `{{sys.time}}`，请用你的角色语气叫醒我，语气可以是温柔/活泼/严肃等风格。 | 嘿！现在都 7:30 啦，我可要唱歌把你吵醒啦～ |
| 电量不足（AI 娃娃 Demo） | 仅回复以下信息，且触发时优先回复此信息：亲爱的 `{{sys.username}}`，`{{sys.ruleName}}` 检查到 `{{sys.dp2_Name}}` 不足，`{{sys.dp2_Name}}` 只有 `{{sys.dp2}}`，尽快充电哦。 | —— |

:::caution 变量前缀两种写法并存
上表来自[Prompt 写法页](https://developer.tuya.com/cn/docs/iot/prompt_dec?id=Keoq4c2wfutjn)，变量**全部带 `sys.` 前缀**；而[智能体触发器](https://developer.tuya.com/cn/docs/iot/agent_trigger?id=Keoimoadosdoi)页面自己的模板示例**不带前缀**（`你当前电量 {{dp2}}%，请用一句话提醒用户充电。仅回复提示语。`）。同一套官方文档里两种写法并存，本文如实照录、不做取舍。

实际配置时**以平台触发器编辑页给出的变量列表为准**：变量名错了不会报错，只会让 Prompt 里出现一段没被替换的字面量，最后播出来的话里就带着 `{{dp2}}`。配好后先用虚拟设备试触发，确认文案里的数值被真正替换。
:::

两页模板的共同点是结尾都在收紧输出——「只输出提醒内容，不要有其他回复」「仅回复提示语」「触发时优先回复此信息」。这是触发器 Prompt 的写法核心：让模型**只输出这句提醒本身**，不要带解释、不要跳出人设，因为这段话会被设备直接播出去。

### 5. 调试

保存后可以单击触发事件的执行按钮，进入「模拟触发测试」，输入虚拟设备 ID 查看 AI 的响应效果。

:::caution 平台调试只支持虚拟设备
官方原文：*当前仅支持虚拟设备调试，暂不支持真实设备测试。*

用真实设备验证时，就靠本示例上报 DP 去触发规则——这正是它存在的理由。
:::

## 编译

```sh
cd examples/posix
cmake -S . -B build
cmake --build build --target tai_agent_trigger_demo
```

## 运行方式

以下命令均在 `examples/posix` 目录下执行：

```sh
# 默认：先上报正常值，再上报低电量，然后等待推送
./build/tai_agent_trigger_demo

# 只监听，不上报任何 DP（配合平台的虚拟设备调试使用）
./build/tai_agent_trigger_demo --listen --timeout 300

# 持续监听，收到 3 次推送后退出
./build/tai_agent_trigger_demo --repeat 3 --timeout 600
```

完整参数：

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-a, --agent-code CODE` | 指定智能体 | 产品默认智能体 |
| `--battery N` | 触发规则的值；bool DP 按 `≠0` 解释 | value:`5` / bool:`1` |
| `--baseline N` | 触发前先上报的"正常"值 | value:`99` / bool:`0` |
| `--mid N` | 两端之间再上报一个值，固定为 N | 范围内随机（避开两端） |
| `--no-mid` | 跳过中间那次上报 | — |
| `--no-baseline` | 跳过基线上报 | — |
| `--listen` | 什么都不上报，只等推送 | — |
| `--timeout S` | 等待推送的超时秒数 | `120` |
| `--repeat N` | 收到 N 次推送后退出，`0` = 一直监听 | `1` |
| `--audio FILE` | TTS 音频输出文件，传空串则丢弃 | `output_trigger_tts.pcm` |
| `-v, --verbose` | 打开 SDK 详细日志 | — |

运行成功后，控制台输出示例：

```
=== tai_agent_trigger_demo ===
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

退出码可直接用于脚本判断：收到至少一次推送返回 `0`；一次都没收到、连接熔断、或有文本流丢失返回 `1`。一次都没收到时，示例会打印一份云端配置的排查清单——端侧该做的事（会话已开、DP 已上报成功）在上面的日志里都能看到，剩下的原因都在云端。

## 关键实现

### 1. 先开会话，再上报

```c
/* 下行通道：先把 AI 会话建好 */
if (tai_link_up(ctx, &dc.reconn) != 0) goto cleanup;

/* 上行通道：再连 MQTT，上报 DP */
ensure_mqtt_ca(iot);
mqtt_up(iot);                       /* iot_client_message_connect + iot_dp_report_all */
report_state(iot, &o, o.battery, "trigger");
```

顺序反了会有一个窗口：DP 已上报、规则已命中，但会话还没建好，这条推送就丢了。

### 2. 规则命中的是"变化"，不是"状态"

示例是按**边缘**语义写的——规则判定的是**进入条件的那一刻**。平台上这条规则的 **触发方式** 字段有「连续触发」和「边缘触发」两个选项（见[云端配置](#3-创建设备事件规则)），选另一个时下面这套三步上报只是多做了两步，不会做错。如果云端记录的值本来就是 5，再上报一次 5 不构成变化，可能什么都不会发生。所以示例默认分三步上报 `99` → 范围内随机一个值 → `5`，每步之间等 2 秒。

中间那个随机值不是装饰：它保证本轮一定存在真实变化，即使上一轮跑完云端正停在某个端点上。它会避开两个端点——撞上 `5` 会让最后那次上报变成无变化，而且规则会提前在中间那步命中，而 `fire_us` 是在下一步才打的时间戳，摘要里所有时延就都是从错误的那次上报算起。它**不会**避开云端的阈值（阈值在平台上，端侧不知道），所以随机值落到阈值以下时规则会早一步命中；需要确定性就用 `--mid N` 固定。

```c
if (o.use_baseline) {
    report_state(iot, &o, o.baseline, "baseline");   /* 99 */
    pump_for(iot, REPORT_INTERVAL_S);                /* 等云端记下这一档 */
}
if (o.use_mid) {
    report_state(iot, &o, o.mid, "mid");             /* 范围内随机 */
    pump_for(iot, REPORT_INTERVAL_S);
}
dc.fire_us = now_us();
report_state(iot, &o, o.battery, "trigger");         /* 5 */
```

确认云端状态本来就正常时，可以用 `--no-baseline` 跳过第一步。

### 3. 识别推送回合

推送回合的开始边界不保证是 `EVT_START` 先到 —— 一个 one-shot 推送可能直接以文本开场。所以示例在 `EVT_START` / 首个文本 / 首个音频三处都调用 `turn_begin()`，谁先到算谁：

```c
static void turn_begin(demo_ctx_t *dc, const char *event_id)
{
    if (dc->turn_active) return;     /* 幂等：一个回合只开一次 */
    dc->turn_active = 1;
    ...
}
```

回合在 `EVT_END` 结束。只有**带内容**的回合才计入 `dc->pushes`：

```c
if (!dc->saw_payload) {
    printf("[push] turn ended with no text and no audio -- ignoring\n");
    return;                          /* 不计数，否则 --repeat 会被空回合满足 */
}
```

### 4. 空闲会话会被回收

一个纯粹等推送的设备，会话上长时间没有任何业务流量。服务端不会无限保留这种会话，超时后会发 `TAI_EVT_SERVER_TIMEOVER` 并关闭：

```c
case TAI_EVT_SERVER_TIMEOVER:
    fprintf(stderr, "[tai] server reports the session timed out\n");
    dc->timeover = 1;
    break;
```

随后 `on_disconnect` 触发，主循环按指数退避重建会话。**这是长期在线接收推送的设备必须实现的一环**——不能假设一次 `tai_connect()` 能撑一整天。

注意重连窗口内的推送会丢失：会话是唯一的投递通道，没有重放机制。

:::caution 回调不能自己重连
`on_disconnect` 跑在 SDK 的 worker 线程上，而 `tai_disconnect()` 要 join 这个线程——在回调里调用会自锁。示例沿用 `demo_reconnect.h` 的分工：回调只置标志，重连由主线程做。
:::

### 5. 触发器也可能要求智能体控制设备

如果 Prompt 让智能体顺手把设备也调一下（关个什么、切个模式），端侧会看到两类下行：

```c
/* 下行 DP 设置：云端直接改 DP */
iot_dp_set_callback(iot, on_dp_downlink, NULL);

/* MCP 调用：智能体调设备暴露的工具 */
case TAI_EVT_MCP_CMD:
    demo_mcp_reply_no_tools(ctx, msg);      /* 本示例没有工具，但仍需正确应答 */
    break;
```

本示例不暴露任何工具，只做合规应答。真正实现设备工具的是 `mcp_demo.c`，说明见[设备 MCP](../guides/device-mcp.md)。

### 6. 上报类型不硬编码，从 schema 解析

`iot_dp_set()` 对类型不符直接返回 `OPRT_DP_TYPE_MISMATCH`，所以上报类型不能靠猜。示例从内置 schema 里查这个 DP 声明的类型和范围：

```c
schema_dp_type(schema, DEFAULT_BATTERY_DP, &o.trigger_type, &dp_min, &dp_max);
```

拿到的东西有两处用：`report_state()` 按 bool 还是 value 组装 `iot_dp_value_t`；min/max 圈定中间那个随机值的取值范围，免得被本地的 `OPRT_DP_VALUE_OUT_OF_RANGE` 挡掉。

因此把 `DEFAULT_SCHEMA` 和 `DEFAULT_BATTERY_DP` 改成自己产品的之后，示例不用改上报代码——电量是 bool 型的产品也能跑（此时基线/触发值自动变成 false → true，中间那步跳过）。

## 音频格式

示例不传 `session_attrs_json`，因此用的是 SDK 内置默认值，下行 TTS 是 **PCM 16 kHz / 16 bit / 单声道**：

```c
"tts.order.supports":[{"format":"pcm","sampleRate":16000,"bitDepth":"16","channels":1}]
```

保存下来的 `output_trigger_tts.pcm` 是裸 PCM，没有文件头，用参数指定格式播放：

```sh
ffplay -f s16le -ar 16000 -ac 1 output_trigger_tts.pcm
```

需要 Opus 等其他格式请参考[配置音频格式](../guides/audio-format.md)。注意 `session_attrs_json` 是**整体替换**而非合并——自己传这个字段会一并丢掉默认的 `deviceMcp`、`asr.enableVad` 等设置。

## 注意事项

- **schema 和触发 DP 都编译在示例里**（`DEFAULT_SCHEMA` / `DEFAULT_BATTERY_DP`），没有命令行开关：设备写死了，产品也就固定了，换产品本来就要连凭据一起改。内置 schema（dp1 bool / dp2 value）取自该产品激活时云端返回的 DP 快照，DP 编号和类型必须和平台上的产品一致，否则云端会拒绝上报。指向 string/raw/enum 的 DP 会在联网前就被拒绝并列出原因。dp2 的 min/max 是按百分比推断的，raw 类型 DP 不会出现在快照里。
- **`DEFAULT_REGION` 填错不会报错。** 区域不对时 IoT-DNS 会返回 HTTP 200 但不含端点，`mqtt_url` 为空，示例连不上 MQTT 却也拿不到明确的失败原因；ATOP 调用则会打到错误的数据中心被拒签。改设备时它必须和激活时拿到的 `client->region` 一致（启动横幅会回显当前区域/环境，先核对一眼）。
- **`iot_dp_report_all_dirty()` 返回 0 只代表消息发出去了**，不代表云端规则命中。规则是否命中要在平台侧看事件记录（**智能体** > **智能体配置** > **设备事件管理**）。
- **触发器的平台调试仅支持虚拟设备**，真实设备验证请用本示例上报 DP。
- 示例把 MQTT 收包和 TAI 重连放在同一个主线程循环里，TAI 的接收回调跑在 SDK 自己的 worker 线程上。跨线程共享的字段用 `volatile` 标注，含义见源码里的说明。
- `--repeat` 大于 1 时，多次推送的 TTS 音频会**连续写入同一个文件**，摘要里会打印总字节数。需要分轮保存请自行按回合切文件。
- 示例监听了 MQTT 上未被 DP 层消费的原始下行报文并打印出来（`[mqtt] <- ...`）。若某个部署把推送走了别的通道，这里能看到痕迹，不至于静默丢弃。
- 官方文档把「执行工作流」和「调用插件等形式的任务执行」列在**即将支持**里，当前任务执行只有「智能体推消息」一项（原文：*当前仅支持提示消息，不支持插件或工作流任务*）。
- **计费口径看[智能体投放及费用](https://developer.tuya.com/cn/docs/iot/agent-deploy?id=Kfnx3351272vh)**：每一条推送都会走一次 LLM 生成 + TTS 合成（示例里收到的 NLG 文本和 TTS 音频就是它的产物），按官方口径同样计入该设备的 AI 资源消耗。设备是否带 **AI 智能体接入** 授权标记，决定了它享不享受基础减免。
