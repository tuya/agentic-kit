---
title: Compile-Time Knobs
sidebar_label: Compile-Time Knobs
sidebar_position: 9
---

# Compile-Time Knobs

Every compile-time knob in the SDK — TAI send/receive buffers and scheduling, MQTT timeouts and packet size, the ATOP-over-HTTP buffers, the FreeRTOS task stack — 18 knobs in total — keeps its default **with the subsystem that owns it** (the single integrator-override pickup lives in `common/log.h` — why, below): the FreeRTOS task knobs live in `pal/pal_config_defaults.h`; the per-module knobs live in each module's include/ directory — `modules/iot-client/include/iot_client_config_defaults.h` (MQTT + ATOP HTTP), `modules/rtc-tcp-client/include/tai_config_defaults.h` (TAI buffers and scheduling), and `modules/tuya-ble/include/tuya_ble_config_defaults.h` (no knobs today; why, in that file's banner). The complete story for each knob (units, couplings, pitfalls hit) is in the comments of the file it lives in; this page covers how to override them per product, why the mechanism is shaped this way, and gives the quick-reference tables.

## Three Ways to Override (Pick One) {#三种覆盖方式任选其一}

### Option 1 (recommended): create your own `agentic_kit_config.h` {#方式一推荐自建-agentic_kit_configh}

Write only the knobs you want to change (plain `#define`, no `#ifndef`), and put the file's directory on the include path of **every target that compiles SDK sources** — the SDK picks it up automatically while compiling each source file, no `-D` needed. Whichever subsystem a knob belongs to, it goes in this **one** file (the pickup in `common/log.h` applies it before every `#ifndef` default), so you never need one override file per module:

```c
/* agentic_kit_config.h — only what you change; the rest follows SDK defaults */
#define AGENTIC_KIT_RESPONSE_BUFFER_SIZE  8192   /* the product's DP schema is large */
#define AGENTIC_KIT_TAI_FRAG_BUF_SIZE    16000U  /* ESP32 without PSRAM: shrink the RX reassembly buffer */
```

ESP-IDF project (SDK compiled as a component): put the file anywhere in your project and add one line so the component can see it —

```cmake
# In this component's CMakeLists.txt; the path points at where you keep agentic_kit_config.h
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../kit_opts")
```

Plain CMake project:

```sh
cmake -B build -DCMAKE_C_FLAGS="-I<config directory>"
```

### Option 2: individual `-D` flags {#方式二逐个-d}

Cheapest when touching one or two knobs, or when a CI matrix switches them:

```cmake
target_compile_definitions(my_sdk_target PRIVATE
    AGENTIC_KIT_RESPONSE_BUFFER_SIZE=8192)
```

### Option 3: `AGENTIC_KIT_USER_CONFIG` names an arbitrary file {#方式三-agentic_kit_user_config-指定任意文件名}

The fallback for toolchains without `__has_include` (older armcc, for example). Note the macro value needs **quotes inside quotes** — the most common spelling mistake:

```sh
-DAGENTIC_KIT_USER_CONFIG='"my_kit_opts.h"'   # the file's directory must also be on the include path
```

When set it takes priority over the Option-1 probe (both are never included); do not define the same knob in both places.

## One Iron Rule: Identical for Every Target That Compiles SDK Sources {#一条铁律对所有编译-sdk-源码的-target-保持一致}

Knobs are compile-time constants that directly determine struct layouts and buffer sizes. **Different translation units seeing different values produces no linker error** — `tai_ctx_size()` computes the size with one set of values while the task allocates memory with another, and the overflow happens silently. Whichever way you override, every target that compiles SDK sources (the SDK library, SDK sources compiled directly into the app, test programs) must see the same configuration.

## Why It Is Designed This Way {#为什么这样设计}

**Why the defaults are distributed across subsystems while the override pickup is in one place.** These defaults used to be scattered at their call sites, and buffer sizes are really **product properties** (how large the schema is, whether there is PSRAM, the audio frame length) — choosing them means going over the memory budget item by item, and a production incident review needs to answer at a glance "which knob owns this memory and why that value". Now each default lives with its owning subsystem — the FreeRTOS task in `pal/pal_config_defaults.h`, module knobs in each module's include/ — so budget reviews and code reviews look at the owning file. The **pickup logic** for integrator overrides, though, exists in exactly one place, `common/log.h`: every SDK translation unit includes that header, and every `*_config_defaults.h` includes it first, so by the time any `#ifndef` default takes effect your override is already in place — one `agentic_kit_config.h` moves all 18 knobs, with no need to split overrides per subsystem.

**Why every knob carries the `AGENTIC_KIT_` prefix.** The name collision is not hypothetical: coreMQTT's bundled `core_mqtt_config_defaults.h` defines a same-named `MQTT_SEND_TIMEOUT_MS` (default 20000U) that fought the SDK's 2000U by include order, and `LOG_LEVEL` is claimed by several platform SDKs. The prefix moves these names into the SDK's own namespace — your `-D` no longer hits someone else's macro, and theirs no longer hits yours.

**Why the SDK files are named `_defaults.h`, leaving the plain name `agentic_kit_config.h` to you.** `#include "..."` (quoted) searches the includer's own directory (the SDK's `common/`) before the `-I` path. If the SDK itself occupied the name `agentic_kit_config.h`, your same-named file on any include path would forever be shadowed by the SDK's own. Reserving the plain name for the integrator is the same convention as lwIP's `lwipopts.h`, mbedTLS's `mbedtls_config.h`, and FreeRTOS's `FreeRTOSConfig.h`: **the override file's name belongs to the integrator**.

**Why `#ifndef` defaults plus including your file first, instead of letting you edit the SDK files.** You never touch SDK sources, so upgrades merge cleanly; knobs your file doesn't mention automatically follow the SDK defaults.

## Knob Quick Reference {#旋钮速查}

Defaults and detailed rationale live in each config file's comments; "when to adjust" is the most common scenario hint. Where each table lives: the PAL table in `pal/pal_config_defaults.h`; both iot-client tables in `modules/iot-client/include/iot_client_config_defaults.h`; the TAI table in `modules/rtc-tcp-client/include/tai_config_defaults.h`.

### iot-client: MQTT {#iot-client-mqtt}

| Knob | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_MQTT_MAX_PACKET_SIZE` | 4096 | When a single CONNECT/SUBSCRIBE/PUBLISH packet overflows. **Coupling**: the DP publish gate `DP_MQTT_MAX_PAYLOAD` in `iot_dp.c` derives from it and follows automatically |
| `AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS` | 2000U | Weak networks, large-packet send timeouts |
| `AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS` | 1000U | Lower for a more responsive processing loop, raise to save wakeups |
| `AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS` | 10000U | Connection handshake budget on weak networks |

### iot-client: ATOP over HTTP {#iot-client-atop-over-http}

| Knob | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE` | 1024 | When assembled request headers (request line + headers) no longer fit |
| `AGENTIC_KIT_RESPONSE_BUFFER_SIZE` | 4096 | **Must-read for products with a large DP schema**: the status line, response headers, and encrypted response body share this one buffer; overflow reports `OPRT_COMMUNICATION_ERROR`. The decrypted-JSON cap is roughly 2.8 KB net of headers (see [Generic ATOP Calls](./atop-generic-call)) |

### rtc-tcp-client (TAI 2.1) {#rtc-tcp-client-tai-21}

| Knob | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD` | 4096U | Transport fragmentation cap, also advertised to the server in ClientHello. Lower saves RX memory, costs more fragments |
| `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` | 32000U | Reassembly buffer = the maximum downlink application packet (large Events / MCP commands / context JSON). Often lowered on PSRAM-less ESP32 |
| `AGENTIC_KIT_TAI_TX_HDR_BUF_SIZE` | 256U | Send-side scatter-gather header buffer; rarely touched |
| `AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT` | 512U | Small-frame coalescing threshold; lowering only narrows the coalescing window — safe |
| `AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE` | 1024U | Control-packet assembly buffer, roughly `2×strlen(session JSON) + 115`. Raise to 2048/4096 for session/event-heavy configurations |
| `AGENTIC_KIT_TAI_MAX_ATTRS` | 32 | Maximum attributes per packet; rarely touched |
| `AGENTIC_KIT_TAI_DRAIN_BUDGET_MS` | 150U | Per-round drain budget of the receive worker; affects keepalive/shutdown latency under floods |
| `AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS` | 2000U | Worker idle block cap; affects how fast `tai_disconnect()` responds |
| `AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N` | 50 | Logs every Nth intermediate media frame; 0 = sampling off (all demoted to DEBUG) |

### PAL: FreeRTOS Task {#pal-freertos-任务}

| Knob | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` | 6144 | **The unit is StackType_t words, not bytes** (6144 ≈ 24 KB on a 32-bit platform; the TLS handshake runs on this task — don't cut it to 6 KB by mistake) |
| `AGENTIC_KIT_PAL_FR_TASK_PRIORITY` | `tskIDLE_PRIORITY + 5` | Relative to the audio task and the application's main tasks |
| `AGENTIC_KIT_PAL_FR_TASK_NAME` | `"tai_worker"` | Debug display only |

## Migrating from the Old Names {#从旧名字迁移}

All knobs now carry the `AGENTIC_KIT_` prefix (the collision backstory is above). A mechanical rename of old `-D` flags and override headers is all it takes:

| Old name | New name |
|------|------|
| `MQTT_MAX_PACKET_SIZE` | `AGENTIC_KIT_MQTT_MAX_PACKET_SIZE` |
| `MQTT_SEND_TIMEOUT_MS` | `AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS` |
| `MQTT_RECV_TIMEOUT_MS` | `AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS` |
| `MQTT_CONNECT_TIMEOUT_MS` | `AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS` |
| `REQUEST_HEADER_BUFFER_SIZE` | `AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE` |
| `RESPONSE_BUFFER_SIZE` | `AGENTIC_KIT_RESPONSE_BUFFER_SIZE` |
| `TAI_LOG_MEDIA_SAMPLE_N` | `AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N` |
| `TAI_MAX_FRAGMENT_PAYLOAD` | `AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD` |
| `TAI_FRAG_BUF_SIZE` | `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` |
| `TAI_TX_HDR_BUF_SIZE` | `AGENTIC_KIT_TAI_TX_HDR_BUF_SIZE` |
| `TAI_FRAME_COALESCE_LIMIT` | `AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT` |
| `TAI_TX_CTRL_BUF_SIZE` | `AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE` |
| `TAI_MAX_ATTRS` | `AGENTIC_KIT_TAI_MAX_ATTRS` |
| `TAI_DRAIN_BUDGET_MS` | `AGENTIC_KIT_TAI_DRAIN_BUDGET_MS` |
| `TAI_WORKER_POLL_CAP_MS` | `AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS` |
| `PAL_FR_TASK_STACK_WORDS` | `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` |
| `PAL_FR_TASK_PRIORITY` | `AGENTIC_KIT_PAL_FR_TASK_PRIORITY` |
| `PAL_FR_TASK_NAME` | `AGENTIC_KIT_PAL_FR_TASK_NAME` |

## Names Deliberately Outside the Config Files {#这些名字故意不在-config-文件里}

- **`TUYA_BLE_HAL_LOGI/LOGW/LOGE/HEXDUMP`** — defined in `modules/tuya-ble/include/tuya_ble_prov.h`; they are the public header's port-binding contract, overridden by the port before inclusion.
- **`TUYA_BLE_RX_BUF_SIZE` / `TUYA_BLE_TX_BUF_SIZE` / `TUYA_BLE_TX_QUEUE_DEPTH`** — also in `tuya_ble_prov.h`, but for a different reason: they determine the layout of the public struct `tuya_ble_prov_state_t`, and ports size their own buffers against them, which makes them part of the port API; changing them changes a struct layout that ports must recompile and re-size against (the header itself declares the layout not ABI-stable) — they are not build knobs. This is why tuya-ble has no compile-time knobs today (the banner of `modules/tuya-ble/include/tuya_ble_config_defaults.h` has the full story).
- **`IOT_SDK_SW_VER` / `PV` / `BV` and the per-region ATOP hosts** — `modules/iot-client/include/iot_client_config_defaults.h`; release-managed values, not build knobs (they share the knob header but do not ride the override mechanism).
- **coreMQTT / coreHTTP log routing** — `common/core_mqtt_config.h`, `common/core_http_config.h`; they route coreMQTT/coreHTTP internal logs into the global log facade, they are not build knobs.

## How to Confirm an Override Took Effect {#怎么确认覆盖生效了}

**Compile probe** (the most direct). With **exactly the same include paths and `-D`s as the real build** (if Option 2/3 used `-D`, the probe needs them too), compile this small file — an `#error` means it did not take:

```c
/* probe.c — include the defaults file that defines the knob (all of them pick up your override first) */
#include "iot_client_config_defaults.h"
#if AGENTIC_KIT_RESPONSE_BUFFER_SIZE != 8192
#error "override not picked up"
#endif
```

```sh
cc -I<sdk>/modules/iot-client/include -I<sdk>/common -I<your config dir> -c probe.c   # quiet pass = effective
```

**Behavioral observation.** When an ATOP response overflows, the error log states the current buffer size and suggests the matching `-D` (the default handler's output shape is `HH:MM:SS [E] [module tag]`; `(server said …)` is the HTTP status — the server usually already returned 200 successfully, which is exactly the misunderstanding this line exists to clear):

```text
14:13:15 [E] [iot] HTTP response does not fit: need 6558 B body + 300 B headers, buffer is 4096 B (server said 200). Rebuild with a larger -DAGENTIC_KIT_RESPONSE_BUFFER_SIZE.
```

**Artifact inspection.** After lowering the log level, lines of the removed level stop printing; to confirm DEBUG strings never entered the firmware, search the artifact for a DEBUG message you recognize (`strings <library or object file> | grep '<that message>'` should come up empty). Note that a tag prefix like `[ble]` spans error/warn/debug levels and cannot by itself identify DEBUG output.
