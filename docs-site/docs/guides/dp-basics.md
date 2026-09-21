---
title: DP 数据点：定义、创建与使用
sidebar_label: DP 数据点
sidebar_position: 1.5
---

# DP 数据点：定义、创建与使用

<div className="doc-lead">DP（Data Point，数据点）是涂鸦对产品功能的抽象，也是设备与云端之间每一条数据的契约。本文说明 DP 的定义与关键特性、如何在涂鸦开发者平台创建 DP、如何在 agentic-kit 中上报与接收 DP，最后汇总官方与站内参考文档。</div>

## 什么是 DP {#什么是-dp}

官方文档从三个层面定义 DP：

- **功能抽象**——基础 DP（TuyaOS 文档）：DP 也叫功能点，“是描述一个设备所具有的功能。涂鸦开发者平台将设备的功能都抽象成功能点，支持数值型、布尔型、枚举型、字符串型、故障型以及 Raw 透传数据”。
- **数据模型**——名词解释（TuyaOpen 文档）：DP 是涂鸦对产品功能定义的数据模型，用于描述产品的功能。
- **数据契约**——Agent 开发指南（TuyaOpen 文档）：DP 定义了设备与云端之间流动的每一条数据——传感器读数、开关状态、原始二进制有效载荷等；Agent 通过读写 DP 来观察和控制设备。

以一个两路开关为例：它的能力被抽象为**两个 DP，每个都是布尔型**。DP 具备读写属性——**读**是获取当前值，**写**是改变值。App 打开第一路，就是向 DP 1 写入 `true`；设备把开关的实际状态上报云端，App 与 Agent 才能看到最新状态。

## DP 的关键特性 {#dp-的关键特性}

### 四要素 {#四要素}

| 要素 | 说明 | 示例 |
| --- | --- | --- |
| DPID | 数字标识，取值 1–255；**设备与云端的功能数据通过 DPID 传输** | `1`、`2`、`101` |
| DPCode | 人类可读标识符，支持字母、数字、下划线，以字母开头；面向应用层与多语言场景 | `switch_1`、`temp_current`、`color_mode` |
| 数据类型 | 六种官方类型之一（见下表） | Boolean |
| 约束 | 数值型的 min/max/step/scale/unit；枚举型的取值集合 | 0–100，步长 1，单位 % |

标准功能（又称标准 DPCode）即 **DPID 小于 100** 的功能点，详见[如何创建 DP](#如何创建-dp)。

### 六种数据类型 {#六种数据类型}

| 类型 | 含义 | 示例 |
| --- | --- | --- |
| 布尔型 Bool | 非真即假 | 开关的开/关 |
| 数值型 Value | 可线性调节的数值，带取值范围、间距与单位 | 温度 20–40 ℃ |
| 枚举型 Enum | 自定义的有限取值集合 | 低/中/高档 |
| 故障型 Fault | 用于上报和统计故障，支持多故障，**数据只上报** | 干烧、传感器故障 |
| 字符串型 String | 文本 | 版本号、状态描述 |
| 透传型 Raw | 以二进制形式透传的产品功能 | 传感器原始数据帧 |

### 数据传输模式 {#数据传输模式}

| 模式 | 官方定义 |
| --- | --- |
| 可下发可上报（rw） | 指令数据可以发送给设备，设备数据可以传输给云端 |
| 只上报（ro） | 数据只支持从设备传输给云端 |
| 只下发（wr） | 数据只支持从云端发送给设备 |

### 报文格式 {#报文格式}

Wi-Fi/云链路上，DP 以 `dps` JSON 传输，key 为**字符串形式的 DPID**：

```json
{"dps": {"1": true, "2": 25}}
```

云端侧用 **DPCode + 类型 + 约束**描述 DP：`functions` 为可下发的指令集，另有同构的 `status` 状态集（可上报，此处省略）。以官方文档中的电压力锅品类为例，节选：

```json
{
  "result": {
    "category": "dylg",
    "functions": [
      {"code": "switch", "type": "Boolean", "values": "{}"},
      {"code": "appointment_time", "type": "Integer",
       "values": "{\"unit\":\"min\",\"min\":0,\"max\":1440,\"scale\":0,\"step\":1}"}
    ]
  }
}
```

云开放平台（OpenAPI）下发设备指令的接口为 `POST /v1.0/iot-03/devices/{device_id}/commands`，请求体 `{"commands":[{"code":"<dpCode>","value":...}]}`——云端语义使用 DPCode。蓝牙链路则使用二进制帧：`dp_id`（1 字节）+ `dp_type`（1 字节）+ `dp_data_len`（2 字节）+ `dp_data_value`，线上类型编号为 raw=0、bool=1、value=2、string=3、enum=4、bitmap（故障型）=5。

<div className="doc-callout doc-callout--info"><b>DP 是三方共享的契约</b><p>DP 定义在<b>设备固件、涂鸦云、App/Agent 三方共享</b>，只改一处会破坏通信。设计要点：按语义能力建模 DP，而不是映射寄存器值；DPCode 保持人类可读（<code>hvac_mode</code> 优于 <code>dp5</code>，Agent/LLM 靠名字理解语义）；Agent 先读状态再执行动作；控制类 DP 保持幂等（网络故障时写入会被重试）。</p></div>

<div className="doc-callout doc-callout--neutral"><b>云端限流</b><p>单个 DPID 每天上报不超过 <b>3500 条</b>，每 10 分钟不超过 <b>1200 条</b>，超限会被云端限制，因此变化宜合并上报。此外，App 下发 DP 后，需设备上报对应 DP 才会更新 App 端显示。</p></div>

## 如何创建 DP {#如何创建-dp}

DP 在[涂鸦 IoT 平台](https://iot.tuya.com)上随产品功能定义创建：

1. 进入[产品开发](https://platform.tuya.com/pmg/list)，按品类**创建产品**；
2. 在 **01 功能定义 > 产品功能**中添加功能：从标准功能库勾选，或新建自定义功能；
3. 为每个功能填写 DPCode、选择数据类型、设置约束——**每个功能即成为一个 DP**；
4. 功能定义完成后，再进入固件开发与调试（把 schema 接入 agentic-kit，见下文）。

平台把产品功能分为三类：

| 分类 | 说明 |
| --- | --- |
| 标准功能 | 某一产品品类的常用功能，又称标准 DPCode（DPID 小于 100） |
| 自定义功能 | 按产品需求自行定义的功能点 |
| 高级功能 | 云定时、网页跳转等不适合常用表达格式的功能 |

<div className="doc-callout doc-callout--info"><b>自定义功能 + Agent 的额外一步</b><p>若产品含自定义功能点且要供 Agent 使用，还需在平台的 <b>AI 产品指令配置</b>中配置自控指令；全部使用标准功能（DPID 小于 100）时默认已配置好。</p></div>

功能构思示例（参考官方文档中的电压力锅品类）：

| 功能 | DPCode | 数据类型 | 传输方式 | 说明 |
| --- | --- | --- | --- | --- |
| 开关 | `switch` | Bool | 可下发可上报 | 整机启停 |
| 预约时间 | `appointment_time` | Value | 可下发可上报 | 0–1440 min，步长 1 |
| 烹饪模式 | `cook_mode` | Enum | 可下发可上报 | 米饭/粥/汤 |
| 锅内温度 | `temp_current` | Value | 只上报 | 实时温度 |
| 故障 | `fault` | Fault | 只上报 | 干烧/传感器故障，多故障位 |

## 在 agentic-kit 中使用 DP {#在-agentic-kit-中使用-dp}

schema 是平台产品功能定义的 JSON 表达，逐 DP 描述 `id`、`mode`（rw/ro/wr）与 `property`（数据类型与约束），由激活响应或平台导出获得。

### schema 进入设备的三条途径 {#schema-进入设备的三条途径}

| 途径 | 时机 | 说明 |
| --- | --- | --- |
| 首次激活 | 激活响应 | 携带 schema 与 schema_id（见 `examples/posix/pair/api-activate/`） |
| 重启回灌 | 启动时 | 通过 `iot_client_config_t` 的 `schema`/`schema_id`/`dp_state` 回灌，`iot_client_init()` 自动重建 DP registry 并恢复状态（不置 dirty、不上报） |
| 运行期升级 | 周期轮询 | `iot_dp_schema_check_update()` 拉取最新 schema，保留仍存在 DP 的当前值、新增 DP 填默认值，并触发升级回调 |

### 设备侧 schema {#设备侧-schema}

设备侧使用**真实 Tuya schema 格式**（取自 `examples/posix/dp-management/`）：

```json
[
  {"mode":"rw","property":{"type":"bool"},"id":1,"type":"obj"},
  {"mode":"ro","property":{"min":0,"max":100,"scale":0,"step":1,"type":"value"},"id":2,"type":"obj"},
  {"mode":"rw","property":{"min":0,"max":100,"scale":0,"step":1,"type":"value"},"id":3,"type":"obj"},
  {"mode":"ro","property":{"range":["none","charging","charge_done"],"type":"enum"},"id":4,"type":"obj"},
  {"mode":"ro","property":{"type":"raw"},"id":5,"type":"obj"}
]
```

顶层 `type:"obj"` 表示对象型 DP，数据类型在 `property.type`；`mode` 取值 rw/ro/wr，对应上文的传输模式。

### 类型与约束映射 {#类型与约束映射}

| 官方类型 | `iot_dp_type_t` | 存储形式 |
| --- | --- | --- |
| Bool | `IOT_DP_TYPE_BOOL` | `bool` |
| Value | `IOT_DP_TYPE_VALUE` | `int32_t`，受 vmin/vmax 强校验 |
| Enum | `IOT_DP_TYPE_ENUM` | schema `range[]` 的下标 |
| String | `IOT_DP_TYPE_STRING` | UTF-8 字符串，可选 maxlen |
| Raw | `IOT_DP_TYPE_RAW` | 不透明字节，可选 maxlen |
| Fault | —— | 当前版本未单独建模 |

约束映射：`property.min`/`max` → vmin/vmax（强校验）；枚举 `range` → 合法值表；raw/string 的 `maxlen`。

### 核心 API {#核心-api}

| API | 用途 |
| --- | --- |
| `iot_dp_set()` | 本地写值并置 dirty，不上报 |
| `iot_dp_get()` | 读当前缓存值 |
| `iot_dp_report()` | 校验后立即上报单个 DP |
| `iot_dp_report_all()` | 全量上报；**连接/重连成功后必须由应用调用一次** |
| `iot_dp_report_all_dirty()` | 把当前所有置脏（dirty）的 DP——含本地 set 置脏与上报失败保留脏位的——合并为一条批量上报 |
| `iot_dp_set_callback()` | 注册云端下发回调，按 DP 分发 |
| `iot_dp_validate_json()` | 校验 `{"dps":{...}}` 是否符合 schema |
| `iot_dp_dump_json()` / `iot_dp_restore_json()` | 导出/回灌 DP 状态快照（不含 RAW） |
| `iot_dp_set_save_callback()` | 每次 DP 写入即推送持久化回调（即使值未变也会触发） |
| `iot_dp_schema_check_update()` | 轮询 schema 升级 |
| `iot_dp_set_schema_update_callback()` | 注册 schema 升级通知回调 |

全部签名以 `modules/iot-client/include/iot_dp.h` 为准。

### 典型代码流 {#典型代码流}

取材自 `examples/posix/dp-management/dp_management_demo.c`：

```c
/* 云端下发回调: 按 dp_id 分发, value 仅在回调期间有效 */
static void on_dp_downlink(uint8_t dp_id, const iot_dp_value_t *value, void *ud)
{
    /* 在此驱动硬件/UI */
}

/* 1. init: 用持久化的 schema 重建 DP registry, 校验后恢复 DP 状态 */
iot_client_config_t cfg = {
    .schema    = saved_schema,             /* 平台的产品 schema */
    .schema_id = saved_schema_id,
    .mqtt_disable_auto_connect = true,     /* 连接时机由应用掌握 */
    /* devid / secret_key / local_key / region / env ... */
};
iot_client_t *client = iot_client_init(&cfg);
if (saved_dp_state && iot_dp_validate_json(client, saved_dp_state) == OPRT_OK) {
    iot_dp_restore_json(client, saved_dp_state);   /* 恢复缓存, 不上报 */
}
iot_dp_set_callback(client, on_dp_downlink, NULL);
iot_dp_set_save_callback(client, on_dp_save, NULL);             /* 每次写入即持久化 */
iot_dp_set_schema_update_callback(client, on_schema_update, NULL);   /* schema 升级通知 */

/* 2. 连接成功后立即全量上报 —— 云端只认设备上报 */
iot_client_connect(client);
iot_dp_report_all(client);

/* 3. 稳态循环 */
while (g_running) {
    iot_client_process(client, 200);       /* 泵收包, 分发云端下发 */

    iot_dp_value_t v = { .type = IOT_DP_TYPE_VALUE, .value.integer = 87 };
    iot_dp_set(client, 2, &v);             /* 本地变化, 置 dirty */
    iot_dp_report_all_dirty(client);       /* 脏 DP 合并批量上报 */

    iot_dp_schema_check_update(client);    /* 周期轮询 schema 升级 */
}
```

<div className="doc-callout doc-callout--neutral"><b>行为要点</b><p><b>schema 的 mode 不强制</b>——mode 是云端/App 侧提示，设备 SDK 不校验：设备可以 set/上报任意 DP，云端也可下发任意 DP。</p><p><b>id/type 必须与平台产品定义一致</b>，否则云端拒绝上报。</p><p><b>RAW 以 base64 上报，且不进入持久化快照</b>（dump/restore 往返会丢失 RAW）。</p><p><b>连接/重连后必须 report_all</b>——恢复的值不会自动上报；上报失败自动保留 dirty 位待重试；payload 超限返回 <code>OPRT_DP_PAYLOAD_TOO_LARGE</code>，需用 <code>iot_dp_report()</code> 拆分；schema 为空/缺失时进入 loose 透传模式（不校验直接过）。</p></div>

完整可运行示例见 `examples/posix/dp-management/`；激活时获取 schema 见 `examples/posix/pair/api-activate/`；AI 会话中使用 DP 见 `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`。跨重启保存与恢复 DP 状态的完整做法见 [DP 状态持久化](./dp-persistence.md)。

## DP 还是端侧 MCP {#dp-还是端侧-mcp}

稳定、可枚举的设备能力与状态（开关、亮度、温度、模式）用 **DP**——主要调用方是 App、云端自动化与设备联动；动态任务型、带复杂参数的工具（读传感器、拍照、控制电机）用**端侧 MCP**——主要调用方是 AI Agent。二者可在同一产品中并存，选择依据详见 [核心概念：物理设备控制——DP 与端侧 MCP](../concepts.md#物理设备控制dp-与端侧-mcp)。

## 参考文档 {#参考文档}

官方文档：

- [基础 DP - TuyaOS](https://developer.tuya.com/cn/docs/iot-device-dev/Pet_device_dp?id=Kfg93rponqtqo)——DP 定义、数据类型与传输模式的正源之一。
- [名词解释 - TuyaOpen](https://tuyaopen.ai/zh/docs/advanced-use/terminologies)——DP 等核心名词。
- [创建新产品 - TuyaOpen](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/creating-new-product)——平台创建产品与功能定义流程。
- [Agent 开发指南 - TuyaOpen](https://tuyaopen.ai/zh/docs/ide/agent-development)——Agent 视角的 DP 语义与最佳实践。
- [蓝牙 DP 数据格式 - TuyaOS](https://developer.tuya.com/cn/docs/iot-device-dev/bluetooth_software_map_bt_dp_data?id=Kcmeae40r8zdq)——蓝牙二进制 DP 帧布局与类型编号。
- [小程序 DP API - 涂鸦开发者平台](https://developer.tuya.com/cn/miniapp/develop/ray/api/device-control/dp/publishDpsWithPipeType)——小程序侧 DP 下发接口。

站内文档：

- [核心概念：物理设备控制——DP 与端侧 MCP](../concepts.md#物理设备控制dp-与端侧-mcp)
- [DP 状态持久化](./dp-persistence.md)——跨重启保存与恢复 DP 状态。
- [IoT Client API 参考](../reference/iot-client.md)——激活、MQTT 连接等 iot-client 核心 API。
- `examples/posix/dp-management/`——完整的 DP 管理示例。
