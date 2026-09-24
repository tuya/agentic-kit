---
title: "DP (Data Point): Definition, Creation, and Usage"
sidebar_label: DP (Data Point)
sidebar_position: 1.5
---

# DP (Data Point): Definition, Creation, and Usage

<div className="doc-lead">A DP (Data Point) is Tuya's abstraction of a product capability and the contract for every piece of data flowing between the device and the cloud. This guide explains what a DP is, its key characteristics, how to create DPs on the Tuya Developer Platform, how to report and receive DPs in agentic-kit, and closes with a roundup of official and on-site references.</div>

## What Is a DP {#什么是-dp}

The official documentation defines a DP at three levels (quoted passages are English translations of the Chinese originals):

- **Capability abstraction** — Basic DP (TuyaOS docs): a DP, also called a function point, "describes a capability of a device. The Tuya Developer Platform abstracts all device capabilities into function points, supporting Value, Boolean, Enum, String, Fault, and Raw (pass-through) types."
- **Data model** — Terminologies (TuyaOpen docs): a DP is Tuya's data model for product capability definitions, used to describe the functions of a product.
- **Data contract** — Agent Development Guide (TuyaOpen docs): a DP defines every piece of data flowing between the device and the cloud — sensor readings, switch states, raw binary payloads, and so on. An Agent observes and controls a device by reading and writing DPs.

Take a two-gang switch: its capabilities are abstracted as **two DPs, each of Boolean type**. A DP carries read/write semantics — **read** fetches the current value, **write** changes it. When the app turns on the first gang, it writes `true` to DP 1; when the device reports the actual switch states to the cloud, the app and the Agent can see the latest state.

## Key Characteristics of a DP {#dp-的关键特性}

### The Four Elements {#四要素}

| Element | Description | Example |
| --- | --- | --- |
| DPID | Numeric identifier, 1–255; **functional data between the device and the cloud is transported by DPID** | `1`, `2`, `101` |
| DPCode | Human-readable identifier: letters, digits, and underscores, starting with a letter; aimed at the application layer and multilingual scenarios | `switch_1`, `temp_current`, `color_mode` |
| Data type | One of the six official types (see below) | Boolean |
| Constraint | min/max/step/scale/unit for Value; the value set for Enum | 0–100, step 1, unit % |

Standard functions (also called standard DPCodes) are the function points with **DPIDs below 100**; see [Creating DPs](#如何创建-dp).

### The Six Data Types {#六种数据类型}

| Type | Meaning | Example |
| --- | --- | --- |
| Boolean | Either true or false | Switch on/off |
| Value | A linearly adjustable number with range, step, and unit | Temperature 20–40 °C |
| Enum | A custom, finite value set | Low/Medium/High |
| Fault | For reporting and counting faults; supports multiple faults; **report-only** | Dry burn, sensor fault |
| String | Text | Version string, status description |
| Raw | Product functions passed through as binary | Raw sensor data frame |

### Data Transfer Modes {#数据传输模式}

| Mode | Official definition |
| --- | --- |
| Read/write (rw) | Commands can be sent to the device, and device data can be transmitted to the cloud |
| Report-only (ro) | Data can only travel from the device to the cloud |
| Write-only (wr) | Data can only be sent from the cloud to the device |

### Message Formats {#报文格式}

On the Wi-Fi/cloud link, DPs travel as `dps` JSON whose keys are **DPIDs in string form**:

```json
{"dps": {"1": true, "2": 25}}
```

On the cloud side, a DP is described by **DPCode + type + constraints**: `functions` is the command set that can be sent to the device, and there is a structurally identical `status` state set that can be reported (omitted here). Excerpt from the electric pressure cooker category in the official docs:

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

The cloud open-platform API for sending device commands is `POST /v1.0/iot-03/devices/{device_id}/commands` with body `{"commands":[{"code":"<dpCode>","value":...}]}` — the cloud-side semantics use the DPCode. The Bluetooth link uses binary frames instead: `dp_id` (1 byte) + `dp_type` (1 byte) + `dp_data_len` (2 bytes) + `dp_data_value`, with the on-wire type codes raw=0, bool=1, value=2, string=3, enum=4, and bitmap (Fault)=5.

<div className="doc-callout doc-callout--info"><b>A DP is a contract shared by three parties</b><p>DP definitions are <b>shared by the device firmware, the Tuya cloud, and the app/Agent</b>; changing only one of them breaks communication. Design guidance: model DPs after semantic capabilities rather than register values; keep DPCodes human-readable (<code>hvac_mode</code> over <code>dp5</code> — Agents/LLMs understand them by name); have the Agent read state before acting; keep control DPs idempotent (writes are retried on network failures).</p></div>

<div className="doc-callout doc-callout--neutral"><b>Cloud rate limits</b><p>A single DPID may report at most <b>3500 messages per day</b> and <b>1200 per 10 minutes</b>; exceeding the limit gets throttled by the cloud, so merge changes into fewer reports. Also, after the app sends a DP, the device must report the corresponding DP before the app display updates.</p></div>

## Creating DPs {#如何创建-dp}

DPs are created on the [Tuya IoT Platform](https://iot.tuya.com) as part of product function definition:

1. Go to [Product Development](https://platform.tuya.com/pmg/list) and **create a product** under a category.
2. Under **01 Function Definition > Product Functions**, add functions: pick from the standard function library, or create custom functions.
3. For each function, fill in the DPCode, choose the data type, and set the constraints — **each function becomes one DP**.
4. Once the function definition is done, move on to firmware development and debugging (wiring the schema into agentic-kit, below).

The platform groups product functions into three classes:

| Class | Description |
| --- | --- |
| Standard functions | Common functions of a product category, also called standard DPCodes (DPIDs below 100) |
| Custom functions | Function points you define for your product |
| Advanced functions | Functions that do not fit the common expression formats, such as cloud timers and web links |

<div className="doc-callout doc-callout--info"><b>Custom functions + Agents: one extra step</b><p>If the product contains custom function points that an Agent will use, you must also configure autonomous-control commands under <b>AI product command configuration</b> on the platform. Products using only standard functions (DPIDs below 100) are configured by default.</p></div>

An example function worksheet (modeled on the electric pressure cooker in the official docs):

| Function | DPCode | Data type | Transfer mode | Notes |
| --- | --- | --- | --- | --- |
| Switch | `switch` | Boolean | Read/write | Master on/off |
| Appointment time | `appointment_time` | Value | Read/write | 0–1440 min, step 1 |
| Cooking mode | `cook_mode` | Enum | Read/write | Rice/Porridge/Soup |
| Pot temperature | `temp_current` | Value | Report-only | Live temperature |
| Fault | `fault` | Fault | Report-only | Dry burn/sensor fault, multiple fault bits |

## Using DPs in agentic-kit {#在-agentic-kit-中使用-dp}

The schema is the JSON expression of the platform's product function definition: per DP it describes the `id`, the `mode` (rw/ro/wr), and the `property` (data type and constraints). It is obtained from the activation response or exported from the platform.

### Three Ways the Schema Reaches the Device {#schema-进入设备的三条途径}

| Path | When | Description |
| --- | --- | --- |
| First activation | Activation response | Carries the schema and schema_id (see `examples/posix/pair/api-activate/`) |
| Restore on reboot | At startup | Fed back through the `schema`/`schema_id`/`dp_state` fields of `iot_client_config_t`; `iot_client_init()` rebuilds the DP registry and restores state automatically (no dirty bit, no report) |
| Runtime upgrade | Periodic polling | `iot_dp_schema_check_update()` fetches the newest schema, keeps the current values of DPs that still exist, fills defaults for new DPs, and fires the update callback |

### The Device-Side Schema {#设备侧-schema}

The device consumes the **real Tuya schema format** (taken from `examples/posix/dp-management/`):

```json
[
  {"mode":"rw","property":{"type":"bool"},"id":1,"type":"obj"},
  {"mode":"ro","property":{"min":0,"max":100,"scale":0,"step":1,"type":"value"},"id":2,"type":"obj"},
  {"mode":"rw","property":{"min":0,"max":100,"scale":0,"step":1,"type":"value"},"id":3,"type":"obj"},
  {"mode":"ro","property":{"range":["none","charging","charge_done"],"type":"enum"},"id":4,"type":"obj"},
  {"mode":"ro","property":{"type":"raw"},"id":5,"type":"obj"}
]
```

The top-level `type:"obj"` marks an object DP; the data type lives in `property.type`. `mode` is rw/ro/wr, matching the transfer modes above.

### Type and Constraint Mapping {#类型与约束映射}

| Official type | `iot_dp_type_t` | Storage |
| --- | --- | --- |
| Boolean | `IOT_DP_TYPE_BOOL` | `bool` |
| Value | `IOT_DP_TYPE_VALUE` | `int32_t`, strictly validated against vmin/vmax |
| Enum | `IOT_DP_TYPE_ENUM` | Index into the schema `range[]` |
| String | `IOT_DP_TYPE_STRING` | UTF-8 string, optional maxlen |
| Raw | `IOT_DP_TYPE_RAW` | Opaque bytes, optional maxlen |
| Fault | — | Not modeled separately in the current version |

Constraint mapping: `property.min`/`max` → vmin/vmax (strictly validated); the Enum `range` → the table of legal values; `maxlen` for raw/string.

### Core API {#核心-api}

| API | Purpose |
| --- | --- |
| `iot_dp_set()` | Write a value locally and mark it dirty; does not report |
| `iot_dp_get()` | Read the current cached value |
| `iot_dp_report()` | Validate and immediately report a single DP |
| `iot_dp_report_all()` | Report every known DP; **the application must call it once after every connect/reconnect** |
| `iot_dp_report_all_dirty()` | Report every currently dirty DP — those set locally and those left dirty by a failed report — in one batched message |
| `iot_dp_set_callback()` | Register the cloud downlink callback, dispatched per DP |
| `iot_dp_validate_json()` | Validate a `{"dps":{...}}` payload against the schema |
| `iot_dp_dump_json()` / `iot_dp_restore_json()` | Export/restore the DP state snapshot (RAW excluded) |
| `iot_dp_set_save_callback()` | Persistence callback pushed on every DP write (fires even when the value is unchanged) |
| `iot_dp_schema_check_update()` | Poll for a schema upgrade |
| `iot_dp_set_schema_update_callback()` | Register the schema-upgrade notification callback |

All signatures are authoritative in `modules/iot-client/include/iot_dp.h`.

### A Typical Code Flow {#典型代码流}

Adapted from `examples/posix/dp-management/dp_management_demo.c`:

```c
/* Cloud downlink callback: dispatched per dp_id; value is valid only during the call */
static void on_dp_downlink(uint8_t dp_id, const iot_dp_value_t *value, void *ud)
{
    /* Drive hardware/UI here */
}

/* 1. init: rebuild the DP registry from the persisted schema,
 *        then restore DP state after validating it */
iot_client_config_t cfg = {
    .schema    = saved_schema,             /* the product schema from the platform */
    .schema_id = saved_schema_id,
    .mqtt_disable_auto_connect = true,     /* the application owns connect timing */
    /* devid / secret_key / local_key / region / env ... */
};
iot_client_t *client = iot_client_init(&cfg);
if (saved_dp_state && iot_dp_validate_json(client, saved_dp_state) == OPRT_OK) {
    iot_dp_restore_json(client, saved_dp_state);   /* restore cache, no report */
}
iot_dp_set_callback(client, on_dp_downlink, NULL);
iot_dp_set_save_callback(client, on_dp_save, NULL);             /* persist on every write */
iot_dp_set_schema_update_callback(client, on_schema_update, NULL);   /* schema-upgrade notice */

/* 2. Right after connecting, report the full state — the cloud only trusts device reports */
iot_client_connect(client);
iot_dp_report_all(client);

/* 3. Steady-state loop */
while (g_running) {
    iot_client_process(client, 200);       /* pump the receive path, dispatch downlinks */

    iot_dp_value_t v = { .type = IOT_DP_TYPE_VALUE, .value.integer = 87 };
    iot_dp_set(client, 2, &v);             /* local change, marks dirty */
    iot_dp_report_all_dirty(client);       /* batch-report dirty DPs */

    iot_dp_schema_check_update(client);    /* poll for schema upgrades */
}
```

<div className="doc-callout doc-callout--neutral"><b>Behavior notes</b><p><b>The schema mode is not enforced</b> — mode is a cloud/app-side hint that the device SDK does not check: the device may set/report any DP, and the cloud may send any DP via downlink.</p><p><b>id/type must match the product definition on the platform</b>, otherwise the cloud rejects reports.</p><p><b>RAW is reported as base64 and never enters the persistence snapshot</b> (a dump/restore round trip loses RAW values).</p><p><b>report_all after every connect/reconnect</b> — restored values are not reported automatically; failed reports keep their dirty bit for retry; an oversized payload returns <code>OPRT_DP_PAYLOAD_TOO_LARGE</code> and must be split with <code>iot_dp_report()</code>; with an empty or missing schema the layer runs in loose pass-through mode (no validation).</p></div>

For a complete runnable sample see `examples/posix/dp-management/`; for obtaining the schema at activation see `examples/posix/pair/api-activate/`; for using DPs inside an AI session see `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`. The full procedure for saving and restoring DP state across reboots is covered in [DP State Persistence](./dp-persistence.md).

## DP or Device-Side MCP {#dp-还是端侧-mcp}

Use **DPs** for stable, enumerable device capabilities and states (switches, brightness, temperature, modes) — the main callers are apps, cloud automations, and device linkages. Use **device-side MCP** for dynamic, task-shaped tools with complex parameters (reading sensors, taking photos, driving motors) — the main caller is the AI Agent. The two can coexist in one product; for the decision criteria see [Core Concepts: Physical device control — DPs and device-side MCP](../concepts.md#物理设备控制dp-与端侧-mcp).

## References {#参考文档}

Official documentation:

- [Basic DP - TuyaOS](https://developer.tuya.com/cn/docs/iot-device-dev/Pet_device_dp?id=Kfg93rponqtqo) — one authoritative source for DP definitions, data types, and transfer modes.
- [Terminologies - TuyaOpen](https://tuyaopen.ai/zh/docs/advanced-use/terminologies) — core terms including DP.
- [Creating a New Product - TuyaOpen](https://tuyaopen.ai/zh/docs/cloud/tuya-cloud/creating-new-product) — the platform flow for creating products and function definitions.
- [Agent Development Guide - TuyaOpen](https://tuyaopen.ai/zh/docs/ide/agent-development) — DP semantics and best practices from the Agent's perspective.
- [Bluetooth DP Data Format - TuyaOS](https://developer.tuya.com/cn/docs/iot-device-dev/bluetooth_software_map_bt_dp_data?id=Kcmeae40r8zdq) — the Bluetooth binary DP frame layout and type codes.
- [Miniapp DP API - Tuya Developer Platform](https://developer.tuya.com/cn/miniapp/develop/ray/api/device-control/dp/publishDpsWithPipeType) — the miniapp-side DP publish interface.

On this site:

- [Core Concepts: Physical device control — DPs and device-side MCP](../concepts.md#物理设备控制dp-与端侧-mcp)
- [DP State Persistence](./dp-persistence.md) — saving and restoring DP state across reboots.
- [IoT Client API reference](../reference/iot-client.md) — activation, MQTT connection, and other iot-client core APIs.
- `examples/posix/dp-management/` — the complete DP management sample.
