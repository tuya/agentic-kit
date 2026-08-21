---
title: 智能体触发器（主动推送）
sidebar_label: 智能体触发器
sidebar_position: 5
---

# 智能体触发器（主动推送）

> 对应示例：
>      * `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`

:::note 前置条件
- 设备凭据（`devid`、`secret_key`、`local_key`）—— **编译进示例**（源码顶部的 `DEVICE_*` 宏），无需命令行传入。换设备请改这几个宏：触发器 demo 只对配好了事件规则和触发器的那个产品（PID）有意义，而产品又决定了 schema，所以换设备本来就得连 schema 和云端规则一起换。凭据由[配网](./scan-by-device.md)获得。
- **云端必须先配置好触发器**，否则示例只会上报 DP 然后超时退出。云端配置步骤见下文[云端配置](#云端配置)。
- 产品已关联智能体，参见[创建 Agent](../guides/create-agent.md)。
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
① 设备事件规则（云端配置）      dp109(电量) < 20
        ↓ 命中
② 智能体触发器（云端配置）      任务 = 智能体推消息
                                Prompt = "当前电量 {{dp109}}%，用一句话提醒充电"
        ↓ 推送
③ 设备端（本示例）              保持 AI 会话在线，接收并播放这段话
```

示例把端侧的两条通道同时跑起来 —— 一条上行、一条下行，分属两个模块：

```
        ┌──────────────────────── 设备（agent_trigger_demo）─────────────────────┐
        │                                                                        │
        │  iot-client (MQTT)                          rtc-tcp-client (TAI 会话)   │
        │       │  上行：iot_dp_report_all_dirty()          │  下行：空闲会话      │
        │       │  DP109=15                                 │  等待服务端开回合   │
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

以官方文档的"AI 娃娃电量过低提醒"为例。完整步骤参见 [使用触发器自动化](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/ai-agent/agent-trigger-index)。

### 1. 创建设备事件规则

在[涂鸦开发者平台](https://platform.tuya.com/)的产品下创建设备事件，用 DP 条件描述"什么情况算一次事件"：

```
dp109（电量）   < 20
```

示例产品有两个数值型 DP（108/109），规则用 109；没有 enum DP，所以规则是单条件。产品若另有 enum DP（例如充电状态），可以再加一条 `且` 条件，并用 `--charge-dp N` 让示例一并上报。创建完成后**记得启用该规则**，未启用的规则不会产生事件。

### 2. 配置智能体触发器

在智能体开发页面新建触发器：

| 字段 | 说明 |
|------|------|
| 触发器名称 | 自定义 |
| 触发器类型 | 选择「设备事件触发」 |
| 触发事件 | 选择上一步创建的设备事件 |
| 任务执行 | 「智能体推消息」（当前唯一选项） |
| 提示内容（Prompt） | 决定推什么话，可插入动态变量 |

Prompt 里可以插入变量引用触发时的上下文。官方文档出现过的变量：

| 变量 | 含义 |
|------|------|
| `{{sys.dp2}}` | DP2 的值（`dp` 后跟 DP 编号） |
| `{{sys.dp2_Name}}` | DP2 的功能名称 |
| `{{sys.dp}}` | 触发该规则的 DP 值 |
| `{{sys.dp_temperature}}` / `{{sys.dp_humidity}}` | 按标识符引用的 DP 值 |
| `{{sys.username}}` | 用户名 |
| `{{sys.ruleName}}` | 触发的规则名称 |
| `{{sys.time}}` | 当前时间 |

官方给出的 Prompt 模板示例（原文照录）：

| 触发场景 | Prompt 模板 | 生成效果 |
|---------|------------|---------|
| 电量过低 | 你当前的电量是 `{{dp2}}%`，请你用一句话提醒我（用户）给你充电。不允许回复其他无关内容。 | 我快没电了，快给我充电吧～ |
| 温度过高 | 当前室温为 `{{dp4}}℃`，请你说一句话提醒主人降温。不允许回复其他无关内容。 | 屋里太热了，能开下空调吗？ |
| 空气质量差 | 当前空气质量为 `{{dp5}}`，请你说一句提醒主人注意通风换气。不允许回复其他无关内容。 | 空气好差，要不要开个窗？ |
| 掉线提醒 | 你掉线了，请你生成一句简洁的提示语，提醒用户检查网络。不允许回复其他无关内容。 | 设备连接异常，请检查网络。 |
| 电量过低（AI 娃娃） | 亲爱的 `{{sys.username}}`，`{{sys.ruleName}}` 检查到 `{{sys.dp2_Name}}` 不足，`{{sys.dp2_Name}}` 只有 `{{sys.dp2}}`，尽快充电哦。 | —— |

:::caution 变量前缀两种写法并存
上表第 1–3 行来自官方[触发器索引页](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/ai-agent/agent-trigger-index)，用的是**不带前缀**的 `{{dp2}}`；而官方[Prompt 写法页](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/ai-agent/12.1-how-to-write-promts)的示例**全部带 `sys.` 前缀**（`{{sys.dp}}`、`{{sys.dp2}}`……）。两页写法不一致，本文如实照录、不做取舍。

实际配置时**以平台触发器编辑页给出的变量列表为准**：变量名错了不会报错，只会让 Prompt 里出现一段没被替换的字面量，最后播出来的话里就带着 `{{dp2}}`。配好后先用虚拟设备试触发，确认文案里的数值被真正替换。
:::

写法要点见官方 [如何编写智能体触发器消息 Prompt](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/ai-agent/12.1-how-to-write-promts)——核心是要求模型**只输出这句提醒本身**（上面每条模板都以"不允许回复其他无关内容"收尾就是这个目的），不要带解释、不要跳出人设。

### 3. 确认关联关系

- 产品已关联该智能体（否则设备连上的是另一个智能体，收不到这个触发器）
- 对应的产品事件规则处于启用状态

:::caution 平台调试只支持虚拟设备
官方文档明确说明：触发器的平台调试功能**仅支持虚拟设备**，需要输入虚拟设备 ID 做模拟触发，暂不支持真实设备测试。用真实设备验证时，就靠本示例上报 DP 来触发规则。
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

# 用自己产品的 schema 和 DP 编号
./build/tai_agent_trigger_demo --schema my_product.json --battery-dp 109

# 产品另有 enum DP 时，让示例一并上报第二个条件
./build/tai_agent_trigger_demo --charge-dp 4

# 只监听，不上报任何 DP（配合平台的虚拟设备调试使用）
./build/tai_agent_trigger_demo --listen --timeout 300

# 持续监听，收到 3 次推送后退出
./build/tai_agent_trigger_demo --repeat 3 --timeout 600
```

完整参数：

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-a, --agent-code CODE` | 指定智能体 | 产品默认智能体 |
| `--schema FILE` | 产品 schema JSON 文件 | 内置示例 schema |
| `--battery-dp N` | 电量 DP 编号 | `109` |
| `--charge-dp N` | 充电状态 DP 编号，`0` = 不上报 | `0` |
| `--battery N` | 用于触发规则的电量值 | `15` |
| `--charge LABEL\|IDX` | 用于触发规则的充电状态（枚举标签或下标） | `none` |
| `--baseline N` | 触发前先上报的"正常"电量 | `80` |
| `--baseline-charge L` | 触发前先上报的"正常"充电状态 | `charging` |
| `--no-baseline` | 跳过基线上报 | — |
| `--listen` | 什么都不上报，只等推送 | — |
| `--timeout S` | 等待推送的超时秒数 | `120` |
| `--repeat N` | 收到 N 次推送后退出，`0` = 一直监听 | `1` |
| `--audio FILE` | TTS 音频输出文件，传空串则丢弃 | `output_trigger_tts.pcm` |
| `-v, --verbose` | 打开 SDK 详细日志 | — |

运行成功后，控制台输出示例：

```
=== tai_agent_trigger_demo ===
Device ID : 6c3540f2…bykbb
Region    : AY / prod
Mode      : report DPs, then wait for a push
[tai] server    : 101.132.65.94:443 (SNI: rtc-ai1-5.tuyacn.com)
[tai] client id : 6c3540f2…bykbb:1786541120:jdVXdg0acvt+H3NQ...
[tai] opening the AI session...
[tai] session open; the demo sends nothing on it -- every turn from here on is server-initiated
[iot] MQTT connected; reporting full DP state
[dp] -> baseline: DP 109 = 80 (rc=0)
[dp] -> trigger: DP 109 = 15 (rc=0)
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
report_state(iot, &o, o.battery, charge_index, "trigger");
```

顺序反了会有一个窗口：DP 已上报、规则已命中，但会话还没建好，这条推送就丢了。

### 2. 规则命中的是"变化"，不是"状态"

设备事件规则判定的是**进入条件的那一刻**。如果云端记录的电量本来就是 15，再上报一次 15 不构成变化，可能什么都不会发生。所以示例默认分两步：

```c
if (o.use_baseline) {
    report_state(iot, &o, o.baseline, baseline_charge_index, "baseline");   /* 80 */
    for (int i = 0; i < BASELINE_SETTLE_S * 5 && g_running; i++)
        iot_client_message_process(iot, 200);                              /* 等云端记下 */
}
dc.fire_us = now_us();
report_state(iot, &o, o.battery, charge_index, "trigger");                  /* 15 / none */
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

### 6. DP 上报的类型安全

枚举 DP 在协议上传的是**下标**，猜错下标就等于上报了另一个状态。示例不硬编码下标，而是从 schema 的 `range[]` 里解析标签：

```c
/* --charge none → 从 schema 的 range[] 找到 "none" 的下标 */
schema_enum_index(schema, o.charge_dp, o.charge, &charge_index);
```

标签不存在时直接列出所有合法值：

```
DP 4 has no enum value "bogus"; valid values are: none(0) charging(1) charge_done(2)
```

`--charge 0` 这样直接给下标也接受——产品的枚举标签和示例不一致时，不必额外准备 schema 文件。

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

- **`--schema` 要用你自己产品的 schema。** 内置 schema（dp101 bool / dp102·dp103·dp105 string / dp108·dp109 value）取自该产品激活时云端返回的 DP 快照，DP 编号和类型必须和平台上的产品一致，否则云端会拒绝上报。其中两个 value DP 的 min/max 是按百分比推断的，raw 类型 DP 不会出现在快照里。
- **`DEVICE_REGION` 填错不会报错。** 区域不对时 IoT-DNS 会返回 HTTP 200 但不含端点，`mqtt_url` 为空，示例连不上 MQTT 却也拿不到明确的失败原因；ATOP 调用则会打到错误的数据中心被拒签。改设备时它必须和激活时拿到的 `client->region` 一致（启动横幅会回显当前区域/环境，先核对一眼）。
- **`iot_dp_report_all_dirty()` 返回 0 只代表消息发出去了**，不代表云端规则命中。规则是否命中要在平台侧看事件记录。
- **触发器的平台调试仅支持虚拟设备**，真实设备验证请用本示例上报 DP。
- 示例把 MQTT 收包和 TAI 重连放在同一个主线程循环里，TAI 的接收回调跑在 SDK 自己的 worker 线程上。跨线程共享的字段用 `volatile` 标注，含义见源码里的说明。
- `--repeat` 大于 1 时，多次推送的 TTS 音频会**连续写入同一个文件**，摘要里会打印总字节数。需要分轮保存请自行按回合切文件。
- 示例监听了 MQTT 上未被 DP 层消费的原始下行报文并打印出来（`[mqtt] <- ...`）。若某个部署把推送走了别的通道，这里能看到痕迹，不至于静默丢弃。
- 涂鸦官方文档以 Wukong SDK v3.12.14+ 为例给出设备端版本要求。使用 agentic-kit 时，端侧的实质要求是**保持 AI Foundation 2.1 会话在线**——也就是本示例演示的内容。
- 官方文档提到触发器的「执行工作流」「通过插件执行任务」属于**计划中的功能**，当前任务执行只有「智能体推消息」一项。
