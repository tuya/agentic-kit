---
title: 设备传递工作流变量
sidebar_label: 工作流变量
---

# 设备传递工作流变量 {#workflow-variables}

`workflow_variables_demo` 使用源码版 RTC TCP Client (`tai_*`) 发送有类型的工作流输入。默认在一个 Session 中依次发送三个 Event：`chat`、`generate-img`、`decide`。使用 `--case ACTION COUNT` 可覆盖默认序列，每组参数产生一个 Event。`count` 仍是工作流的整数输入，**不是发送次数**；三个默认值均为 `1`。示例接收文本和图片，不需要麦克风、Opus 或预编译 RTC 库。

:::warning 已适配新图，尚未实测新版
默认序列对应提供的工作流图：`chat` 输出纯文本，`generate-img` 输出图片，`decide` 输出包含布尔字段 `result` 的 JSON。此版本尚未连接云端执行；下方早期 `sing` 记录属于旧工作流，不能证明新分支可用。
:::

## 前提与平台配置 {#platform}

- 使用已激活设备的 `devid`、`secret_key`、`local_key`，不是激活前的 UUID/authkey。
- 该工作流位于中国数据中心，默认使用 `AY` 和生产环境 `PROD`。其他区域设备必须选择匹配的区域；区域参数不会迁移工作流。
- 主机需要已同步的 UTC 时间、C 编译器、CMake 3.20+、Python 3 和已初始化的依赖子模块。
- 设备所属产品的 Agent 必须关联已发布的工作流，且文本入口必须能够路由到该工作流。参考 [创建 Agent](./create-agent.md) 和 [创建工作流](./create-workflow.md)。默认不传 Agent code，使用产品绑定的 Agent；`--agent-code` 仅透传给 token API，不会建立或修改产品关联。

在平台核对以下图结构（字段名及大小写必须一致）：

| 节点 | 配置 |
|---|---|
| 开始 | `action`: 字符串；`count`: 整数 |
| 选择器第一分支 | `action == "chat"`：大模型输出纯文本 |
| 选择器第二分支 | `action == "generate-img"`：图像生成输出图片 |
| 选择器第三分支 | `action == "decide"`：大模型输出 JSON，包含布尔字段 `result` |
| 默认分支 | 其他 action；示例要求非空文本 |
| 输出 / 结束 | 配置设备下发内容，而非仅配置内部返回值 |

```text
设备 action.value(string), count.value(integer)
    -> EventStart 的 custom.param
    -> 云端解包为工作流变量
    -> 选择器：chat / generate-img / decide / 默认分支
    -> 输出节点 -> 结束（内部结果不等于设备下发内容）
    -> 设备文本或图片回调，随后 EventEnd
```

图只能说明结构，不能证明端到端成功。仍需核对发布版本、产品/设备 Agent 关联和文本入口路由。当前截图未展示开始节点输入；`count` 沿用旧示例，不代表新分支使用该值。本文不提供未经确认的按钮路径。编辑器 URL 中的 `workflowId` 不是 SDK 路由参数，不应作为 Agent code、Agent token 或 `sys.workflow` 值。

输出节点的内部 `output`、`another` 字段和 End 返回值不等于已下发文本。本次 sing 节点虽有内部结果，但 `outputContent:""`，没有 NLG/response 队列；End 也未产生文本队列。需要配置非空的输出内容/模板，并在获得工作流所有者批准后才能修改和发布，再重新验证设备文本响应。不要为复现示例擅自修改生产工作流；本次未进行任何生产配置修改。

## 输入与提示词分开传递 {#data-flow}

应用构造 `custom.param` 的内层对象，每个参数都必须有 `value` 包装：

```json
{"action":{"value":"chat"},"count":{"value":1}}
```

Omni 解析器要求 `Map<String,CustomParamValue>`，不能直接传 `{"action":"sing","count":2}` 这样的标量映射。根因已在示例的 `build_workflow_params()` 中修复：它使用 vendored cJSON 构造各个包装对象，以 `cJSON_AddStringToObject()`、`cJSON_AddNumberToObject()` 添加 `value`，再用 `cJSON_PrintUnformatted()` 序列化并自动转义 action。`action.value` 是 JSON 字符串，`count.value` 是 JSON 数字而非 `"2"`。每个包装对象还可包含可选的 `effectiveCount`，本例省略；它不是工作流输入 `count`，不可混用。SDK 的原始 JSON 透传行为未改变。

SDK 在 EventStart 的 UserData Attribute 111 中添加包装，默认内容形如：

```json
{
  "chatAttributes": "{\"sys.workflow\":\"asr-llm-tts\",\"asr.enableVad\":true,\"tts.alternate\":true,\"processing.interrupt\":true}",
  "sessionAttributes": {
    "custom.param": {"action":{"value":"chat"},"count":{"value":1}}
  }
}
```

`chatAttributes` 是 JSON 编码的**字符串**；`sessionAttributes` 和 `custom.param` 的值是**对象**。`custom.param` 是包含点号的单一键，不是 `custom` 下的 `param`。调用方必须提供每个参数的 `value` 包装，但不要自行添加外层 `sessionAttributes`/`custom.param`，也不要把整个自定义对象编码成字符串。SDK 只添加外层包装，不补齐 `value`，不检查自定义 JSON 的对象形状或字段类型。

提示词单独作为 Text Packet 发送，对应 `USER_TEXT`。默认让 `chat` 描述窗边晒太阳的小猫，让 `generate-img` 画出该场景，让 `decide` 判断猫是否属于动物。`--text` 可将所有提示词覆盖为同一文本。分支通过自定义变量 `action` 选择，不能用提示词代替自定义输入。未知 action 使用 `Please run the configured workflow.`。

## 初始配置与后续 Event {#sdk-usage}

以下为核心调用片段，完整初始化、错误处理和等待逻辑见 `examples/posix/ai/rtc-tcp-client/workflow_variables_demo.c`：

```c
static const char first[] =
    "{\"action\":{\"value\":\"chat\"},\"count\":{\"value\":1}}";
static const char next[] =
    "{\"action\":{\"value\":\"generate-img\"},\"count\":{\"value\":1}}";
static const char prompt[] = "Please run the configured workflow.";

/* Fill the remaining connection, PAL and callback fields before init. */
cfg.event_custom_param_json = first;
/* tai_ctx_init(), tai_connect(), then: */
int rc = tai_send_text(ctx, prompt, sizeof(prompt) - 1);
/* Check rc and wait for the response's EventEnd before continuing. */

/* NULL restores default chat attributes, not "leave unchanged". */
tai_set_event_params(ctx, NULL, next);
rc = tai_send_text(ctx, prompt, sizeof(prompt) - 1);
```

- `tai_set_event_params()` 只修改本地指针，不发送 Packet，也不更新已经打开的 Event。后续 EventStart 才携带新参数。
- 每次提供完整对象；SDK 是替换，不是合并。调用方应串行执行 setter 和产生 Event 的发送操作；setter 本身无锁。
- 配置指针是借用的，不会复制。setter 提供的字符串至少保持到下一次替换且所有使用它的发送结束。示例把所有序列化对象保留到 context 销毁后，避免异步生命周期问题。
- `tai_set_event_params(ctx, NULL, NULL)` 恢复默认 chat attributes，并省略整个 `sessionAttributes`；传 `"{}"` 则发送空的 `custom.param` 对象。省略字段**不代表**云端持久变量被清除，云端合并/保留策略需单独确认。
- 同一个 POSIX PAL 用于 `iot_init(pal)` 和 TAI；先初始化再使用 cJSON。IoT client 禁用 MQTT 自动连接，解析并复制 token 连接参数后即销毁。

API 详情见 [RTC TCP Client](../reference/rtc-tcp-client.md#tai_set_event_params)。

## 构建与运行 {#build-run}

从仓库根目录构建独立 examples 工程；不要与根工程共用 build 目录：

```shell
cmake -S examples/posix -B build-examples \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_C_FLAGS="-DAGENTIC_KIT_LOG_LEVEL=2"
cmake --build build-examples --target workflow_variables_demo -j 4
./build-examples/workflow_variables_demo --help
```

首次配置 examples 会获取 QR/图像依赖，可能需要访问网络，即使本目标不使用它们。WARN 编译选项必须覆盖 SDK C 库：默认 DEBUG 的 ATOP 日志和 INFO 的 TAI Packet 日志可能包含 Session token。仅设置同名 CMake 变量不会传入编译器。示例自己的输入、输出仍会打印，分享前请脱敏。

:::warning TLS 限制
示例保留现有 POSIX TAI 默认 TLS 配置，未提供证书包回调，因此不能把加密连接视为已验证服务端身份。仅用于受控开发环境；生产集成请按 [TLS 证书验证](./tls-cert-verification.md) 配置可信证书验证，不要从本示例推导安全默认值。
:::

在本地安全地设置下列环境变量，以下均为占位符，不要提交真实凭据：

```shell
export TUYA_DEVICE_ID='<activated-device-id>'
export TUYA_SECRET_KEY='<activated-secret-key>'
export TUYA_LOCAL_KEY='<activated-local-key>'

# Default: chat 1, generate-img 1, decide 1, with action-specific prompts
./build-examples/workflow_variables_demo

# Explicit sequence with more time for image generation
./build-examples/workflow_variables_demo \
  --case chat 1 --case generate-img 1 --case decide 1 --timeout 180

# Override the prompt for a single action
./build-examples/workflow_variables_demo --case decide 1 \
  --text 'Is the sun a star? Return a boolean result.'
```

| 选项 | 行为 |
|---|---|
| `--case ACTION COUNT` | 可重复；action 非空，允许未知字符串进入默认分支；count 为完整十进制有符号 32 位整数 |
| `--text TEXT` | 非空覆盖文本，所有 case 共用；未指定时使用上述各 action 专用提示词 |
| `--agent-code CODE` | 可选非空 code；默认产品绑定的 Agent，不是工作流编辑器 ID |
| `--region REGION` | `AY`（默认）、`AZ`、`UEAZ`、`EU`、`WEAZ`、`IN`、`SG`；固定 PROD |
| `--timeout SECONDS` | 每个响应的单调时钟期限，1–3600 秒，默认 60；从发送开始计时，不是整个启动过程的期限 |
| `--help` | 不需要凭据或网络 |

设备 ID 必须为 1–31 字节，两种 key 为 16–31 字节；超长值不会截断。命令行和凭据校验先于联网。

## 结果与限制 {#results}

控制台打印 case 编号、有类型的输入 JSON、`USER_TEXT`、原始文本、解码后的 NLG content、Event 边界和汇总。原始文本保留结构化输出的全部字段，不假定响应必定是裸的 `{"output":...}`。非 NLG 分片通过共享缓冲重组，EventEnd 时补交尚未收到文本 END 的内容；默认 MCP 声明保留，并响应“无工具”的 MCP 请求。

根据 action 判断响应是否完成：

- `chat`：要求非空纯文本，不进行 JSON 解析。
- `generate-img`：要求一个完整、非空的 Image Packet 流（`START` 到 `END`，或 `ONE_SHOT`），最多 16 MiB。主线程在 EventEnd 后将编码字节保存到当前目录下唯一的 `workflow-image-XXXXXX` 文件，并打印路径、字节数及上报格式。仅有文本（包括文本中的图片 URL）不算收到图片；示例不会下载 URL 或解码图片文件。
- `decide`：在整个 Event 内累积解码后的 NLG content，解析为一个完整 JSON 对象，且 `result` 必须为布尔值，例如 `{"result":true}` 或 `{"result":false}`。两种布尔值均有效；字符串 `"true"`、缺失字段、代码围栏或尾随文本会失败。在 `[Workflow JSON parsed]` 下打印所有顶层字段的 JSON 类型和值。
- 其他 action：要求非空文本，保留自定义/默认分支兼容性。

只有各 case 都收到 EventEnd 和预期类型的响应，且图片保存成功，才返回 0。这只是**收到响应**，不能证明工作流分支或决策结果正确。空 NLG 结束标记不算有效文本。参数错误、token/连接/发送失败、断连、ChatBreak、服务端超时、响应期限到达、输出丢失/不完整或缺少预期输出均返回非零。Ctrl-C/SIGTERM 请求退出；同步 SDK 调用返回后执行清理。不会自动重连或重放，避免不确定发送结果造成重复执行。

### 历史实测：旧版 sing/move 工作流

2026-10-22 00:15 CST（UTC+8），cn/prod 的首个 case `sing 2`：

| 环节 | 观测 |
|---|---|
| Graph Chat | `customParam` 为 `{"count":2,"action":"sing"}`；这是云端解包后的值，不是设备发送格式 |
| `getVariables` | 同样为 `{"count":2,"action":"sing"}`，确认整数输入已进入工作流 |
| 选择器 | 命中 sing 输出节点 `output_yPA1` |
| sing 输出节点 | 内部结果 `{"output":"sing","another":"2"}`；`outputContent:""`，没有 NLG/response 队列 |
| End | 输入及结果均为 `{"output":"dumb"}`，没有文本队列 |
| 设备 | 仅收到空的最终 NLG，随后 EventEnd；没有非空文本 |

因此示例在首个 case 后正确以非零状态结束，汇总 0/3；这不是三个 case 都已执行。`move 3`、`other 1` 和同一 action 不同 count 等剩余矩阵尚未验证，也没有端到端成功记录。`another:"2"` 是内部输出字符串，不改变输入 `count` 为整数的事实，更不代表字段已下发。

下一步应由工作流所有者批准配置非空输出内容/模板并发布，再重跑完整矩阵，验证分支、count 和设备收到的文本。可在获批的演示副本中使用包含分支名和 count 的确定性模板；这是建议，不是本次已完成的配置。仅查看内部结果或 End 返回值不能替代设备文本验证，本次未修改生产配置。

## 常见问题 {#troubleshooting}

| 现象 | 排查方向 |
|---|---|
| 返回普通聊天 | 检查产品绑定的 Agent、已发布工作流、文本入口路由；不是把 workflowId 填入 `sys.workflow` |
| token 被拒绝 | 读取返回的 cloud code/message；核对激活状态、区域、产品 Agent、隐私协议，不要盲目重复执行 |
| 修改图后行为不变 | 当前设备可能仍使用旧发布版本或其他 Agent |
| action/count 缺失或类型错误 | 核对每个参数的 `{"value":...}` 包装、大小写、Start 绑定及数字类型的 `count.value`；不要重复包装 `sessionAttributes`，不要把 count 当作 `effectiveCount` |
| `another` 不见了 | 核对输出节点模式及到设备的映射；End 的 output 不会自动包含所有上游字段 |
| EventEnd 但缺少预期输出 | `chat` 需要文本，`generate-img` 需要完整 Image Packet 流，`decide` 需要含布尔字段 `result` 的 JSON；仅内部返回值或文本图片 URL 不足以完成 |
| 超时或断连 | 检查区域、网络、UTC、工作流耗时；`--timeout` 只控制应用响应等待，不覆盖 SDK 连接/发送的内部超时 |

## 相关文档 {#related}

- [创建工作流](./create-workflow.md)
- [创建 Agent](./create-agent.md)
- [RTC TCP Client API](../reference/rtc-tcp-client.md)
