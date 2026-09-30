---
title: Compile-Time Configuration
sidebar_label: Compile-Time Configuration
sidebar_position: 9
toc_labels:
  三种覆盖方式任选其一: Override options
  方式一推荐自建-agentic_kit_configh: Config header (recommended)
  方式二逐个-d: Compiler defines
  方式三-agentic_kit_user_config-指定任意文件名: Custom config filename
  一条铁律对所有编译-sdk-源码的-target-保持一致: Keep targets consistent
  日志编译期闸门单层: Logging configuration
  改写日志分发agentic_kit_log: Custom log output
  这些名字故意不在-config-文件里: Configuration scope
  怎么确认覆盖生效了: Verify overrides
---

# Compile-Time Configuration

The SDK provides **22 compile-time configuration options** for buffers, timeouts, task scheduling, and log levels. This page explains how to override defaults per product, describes the design, and provides quick-reference tables.

**Recommended approach:** put the configuration options you want to change in one `agentic_kit_config.h`. There is no need to edit SDK sources or create separate override files per module.

## Where Defaults Live {#默认值在哪里}

Defaults live with their owning subsystem. Each file's comments document the units, dependencies, and pitfalls:

- **SDK-wide logging** — `AGENTIC_KIT_LOG_LEVEL` and the shared entry point for loading integrator configuration.

  File: `common/log.h`
- **PAL / FreeRTOS** — task stack, priority, and name.

  File: `pal/pal_config_defaults.h`
- **iot-client** — MQTT timeouts and packet size, ATOP HTTP buffers, and the module log ceiling.

  File: `modules/iot-client/include/iot_client_config_defaults.h`
- **rtc-tcp-client** — TAI send/receive buffers, scheduling, and the module log ceiling.

  File: `modules/rtc-tcp-client/include/tai_config_defaults.h`
- **tuya-ble** — the module log ceiling, currently its only configuration option.

  File: `modules/tuya-ble/include/tuya_ble_config_defaults.h`

See the [design rationale](#为什么这样设计) for the shared configuration entry point, and [configuration scope](#这些名字故意不在-config-文件里) for why BLE layout constants are not configuration options.

## Three Ways to Override (Pick One) {#三种覆盖方式任选其一}

### Option 1 (recommended): create your own `agentic_kit_config.h` {#方式一推荐自建-agentic_kit_configh}

1. Create `agentic_kit_config.h` with only the configuration options you want to change. Use plain `#define`, not `#ifndef`.
2. Add its directory to the include path of **every target that compiles SDK sources**.
3. Rebuild the SDK. Each source file picks up the configuration automatically; no extra `-D` is needed.

All subsystems share this **one** override file. `common/log.h` loads it before any `#ifndef` defaults take effect:

```c
/* agentic_kit_config.h — only what you change; the rest follows SDK defaults */
#define AGENTIC_KIT_RESPONSE_BUFFER_SIZE  8192   /* the product's DP schema is large */
#define AGENTIC_KIT_TAI_FRAG_BUF_SIZE    16000U  /* ESP32 without PSRAM: shrink the RX reassembly buffer */
#define AGENTIC_KIT_LOG_LEVEL                2   /* keep error+warn only in production; the cost is in the log section below */
#define AGENTIC_KIT_TAI_LOG_LEVEL            0   /* with the SDK at 2, also silence rtc-tcp-client entirely (its ERROR/WARN lines too; lower-only, see below) */
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

Cheapest when touching one or two configuration options, or when a CI matrix switches them:

```cmake
target_compile_definitions(my_sdk_target PRIVATE
    AGENTIC_KIT_RESPONSE_BUFFER_SIZE=8192
    AGENTIC_KIT_LOG_LEVEL=2)
```

### Option 3: `AGENTIC_KIT_USER_CONFIG` names an arbitrary file {#方式三-agentic_kit_user_config-指定任意文件名}

The fallback for toolchains without `__has_include` (older armcc, for example). Note the macro value needs **quotes inside quotes** — the most common spelling mistake:

```sh
-DAGENTIC_KIT_USER_CONFIG='"my_kit_opts.h"'   # the file's directory must also be on the include path
```

When set it takes priority over the Option-1 probe (both are never included); do not define the same configuration option in both places.

## One Iron Rule: Identical for Every Target That Compiles SDK Sources {#一条铁律对所有编译-sdk-源码的-target-保持一致}

Configuration options are compile-time constants that directly determine struct layouts and buffer sizes. **Different translation units seeing different values produces no linker error** — `tai_ctx_size()` computes the size with one set of values while the task allocates memory with another, and the overflow happens silently. Whichever way you override, every target that compiles SDK sources (the SDK library, SDK sources compiled directly into the app, test programs) must see the same configuration.

## Why It Is Designed This Way {#为什么这样设计}

**Why the defaults are distributed across subsystems while the override pickup is in one place.** These defaults used to be scattered at their call sites, and buffer sizes are really **product properties** (how large the schema is, whether there is PSRAM, the audio frame length) — choosing them means going over the memory budget item by item, and a production incident review needs to answer at a glance "which configuration option owns this memory and why that value". Now each default lives with its owning subsystem — logs in `common/log.h`, the FreeRTOS task in `pal/pal_config_defaults.h`, module configuration options in each module's include/ — so budget reviews and code reviews look at the owning file.

The **pickup logic** for integrator overrides, though, exists in exactly one place, `common/log.h`: every SDK translation unit includes that header, and every `*_config_defaults.h` includes it first, so by the time any `#ifndef` default takes effect your override is already in place — one `agentic_kit_config.h` overrides all 22 configuration options, with no need to split overrides per subsystem.

**Why every configuration option carries the `AGENTIC_KIT_` prefix.** The name collision is not hypothetical: coreMQTT's bundled `core_mqtt_config_defaults.h` defines a same-named `MQTT_SEND_TIMEOUT_MS` (default 20000U) that fought the SDK's 2000U by include order, and `LOG_LEVEL` is claimed by several platform SDKs. The prefix moves these names into the SDK's own namespace — your `-D` no longer hits someone else's macro, and theirs no longer hits yours.

**Why the SDK files are named `_defaults.h`, leaving the plain name `agentic_kit_config.h` to you.** `#include "..."` (quoted) searches the includer's own directory (the SDK's `common/`) before the `-I` path. If the SDK itself occupied the name `agentic_kit_config.h`, your same-named file on any include path would forever be shadowed by the SDK's own. Reserving the plain name for the integrator is the same convention as lwIP's `lwipopts.h`, mbedTLS's `mbedtls_config.h`, and FreeRTOS's `FreeRTOSConfig.h`: **the override file's name belongs to the integrator**.

**Why `#ifndef` defaults plus including your file first, instead of letting you edit the SDK files.** You never touch SDK sources, so upgrades merge cleanly; configuration options your file doesn't mention automatically follow the SDK defaults.

## Logging: a Compile-Time Gate, Single Layer {#日志编译期闸门单层}

`AGENTIC_KIT_LOG_LEVEL` is the global log switch for every SDK module compiled from source and acts only at compile time. The prebuilt rtc-client is the exception: it uses its own runtime interface, described in section 3.5 of its reference page.

| Value | Logs compiled in |
|---|---|
| 0 | None |
| 1 | error |
| 2 | error + warn |
| 3 | error + warn + info |
| 4 (default) | error + warn + info + debug |

Lines above the ceiling vanish at compile time: no function call, no argument evaluation, and no format strings in the firmware, saving flash/RAM.

**There is no runtime level: what compiles in is what prints.** Lines at or below the ceiling emit unconditionally.

When modules need different levels (iot-client at info while rtc-tcp-client stays at debug, say), use the **per-module ceilings**: `AGENTIC_KIT_IOT_LOG_LEVEL`, `AGENTIC_KIT_TAI_LOG_LEVEL` and `AGENTIC_KIT_TUYA_BLE_LOG_LEVEL` all default to the SDK-wide ceiling and can only **lower their one module** further — the effective ceiling is the smaller of the two, so a value above `AGENTIC_KIT_LOG_LEVEL` has no effect (the excess is clamped back to the global value in the module's config file, so block-level gates see the effective ceiling). They gate each module's vocabulary where it is defined (`IOT_LOG*` / `TAI_LOG*` / `TUYA_BLE_HAL_LOG*`, plus rtc-tcp-client's packet-log formatter) and ride the same override pickup as the global ceiling.

The example above — iot at info, rtc at debug — is the default ceiling of 4 plus one line, `-DAGENTIC_KIT_IOT_LOG_LEVEL=3`. Note that the ceiling applies at the level a line actually emits at: tuya-ble's `TUYA_BLE_HAL_LOGI` dispatches at debug, so keeping it takes a 4, not a 3.

Log volume is therefore a build decision: compile production firmware with `-DAGENTIC_KIT_LOG_LEVEL=2` and everything beyond error + warn (code and strings alike) stays out of the image; keep the default 4 in development builds for full logs. The runtime layer is removed wholesale (`log_set_level()`/`log_get_level()`/`tai_set_log_level()` are gone) — the level has no second switch.

**Where lines land is a build decision too**: the default destination is stderr; define `AGENTIC_KIT_LOG` (next section) and every line dispatches into your own macro — the "shape output at runtime" cases (capturing or quieting in a test, say) become a mode inside your macro's target function. The SDK holds no log state of any kind.

> **Migration**: the old per-module `-DTAI_LOG_LEVEL=N` is now `-DAGENTIC_KIT_TAI_LOG_LEVEL=N` (still scoped to rtc-tcp-client, with a semantic change: it can only lower the module below the SDK-wide ceiling; to quiet the whole SDK use `-DAGENTIC_KIT_LOG_LEVEL=N`). Code that called `log_set_level(N)` at boot: drop the call, compile with `-DAGENTIC_KIT_LOG_LEVEL=N` instead, or filter by level inside your `AGENTIC_KIT_LOG` target. Code that installed a handler with `log_set_handler()`: make that function the `AGENTIC_KIT_LOG` target — it now receives the level and the bare tag directly, no need to strip the tag prefix out of the format string anymore.

### Remapping the Dispatch: AGENTIC_KIT_LOG {#改写日志分发agentic_kit_log}

Where logs go has no runtime switch: you decide it at compile time by defining `AGENTIC_KIT_LOG` in the same override file as your configuration options, and every SDK log line dispatches into your own macro. That is precisely the road into macro-based logging systems (ESP-IDF's `ESP_LOGx`, Zephyr's `LOG_*`) — macro to macro, no `va_list` in between (a `va_list` cannot be forwarded to a macro, so any function-bridge design degrades into `vsnprintf` into a buffer, then feeding it back in with `"%s"`: an extra copy, an extra truncation point, and the target macro's format checking lost along the way):

```c
/* agentic_kit_config.h — same file, same pickup as the configuration options;
 * level is the SDK's 1-4 integer, esp_log_level_t needs a mapping */
#define AGENTIC_KIT_LOG(level, tag, fmt, ...)                                   \
    ESP_LOG_LEVEL_LOCAL((level) == LOG_ERROR ? ESP_LOG_ERROR :                  \
                        (level) == LOG_WARN  ? ESP_LOG_WARN :                   \
                        (level) == LOG_INFO  ? ESP_LOG_INFO :                   \
                                              ESP_LOG_DEBUG,                    \
                        tag, fmt, ##__VA_ARGS__)
```

Every log macro in the SDK (iot-client's `IOT_LOG*`, `TAI_LOG*`, `TUYA_BLE_HAL_LOG*`) funnels into `log_tag_*`, which dispatches through `AGENTIC_KIT_LOG` — one definition takes over all of it. You receive the **level, the bare tag, a printf format and its arguments**: the tag as its own token is what makes structured sinks and per-tag filtering possible. The actual values: iot-client always `"iot"`, tuya-ble always `"ble"`, rtc-tcp-client per source file (`"client"`/`"transport"`/`"proto"`/`"crypto"`/`"pkt"`), plus `"mqtt"`/`"http"`/`"tls"`/`"rng"`/`"pal"` from the common and PAL layers.

Two rules, binding both directions:

- **The ceiling still gates**: lines compiled out by `AGENTIC_KIT_LOG_LEVEL` cannot be resurrected by a remap (`log_tag_*` expand to `((void)0)` above the ceiling);
- **The remap must reach every target that compiles SDK sources** — the same iron rule as the configuration options. There is no runtime dispatch to fall back on and no second switch: what compiles in is what prints. The default expansion keeps `log_emit`'s `format(printf)` compile-time checking; your own macro opts out unless you re-add the attribute. If your target is a function rather than a macro, it can call `log_emit_valist()` (the facade's `va_list` entry, the `esp_log_writev` role) to reuse the default stderr output instead of re-implementing the formatting.

The full shape of a function sink — note that `log_emit_valist()` **takes no tag argument**: fold the tag into the format string in your macro, and the output keeps its `[tag]` prefix as the default expansion does:

```c
/* agentic_kit_config.h */
#define AGENTIC_KIT_LOG(level, tag, fmt, ...) \
    my_sink(level, "[" tag "] " fmt, ##__VA_ARGS__)

/* your code (also needs #include <stdarg.h>) */
void my_sink(log_level_t level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_emit_valist(level, fmt, ap);
    va_end(ap);
}
```

## Configuration Reference {#配置项速查}

See [Where Defaults Live](#默认值在哪里) for each subsystem's file path. Those files' comments are authoritative for defaults and detailed rationale; the tables below list common reasons to adjust each configuration option.

### SDK-Wide {#全-sdk}

| Configuration macro | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_LOG_LEVEL` | 4 (debug) | Lower to 1–2 in production (single compile-time layer, see above) |

### Per-Module Log Ceilings {#每模块日志上限}

| Configuration macro | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_IOT_LOG_LEVEL` | = `AGENTIC_KIT_LOG_LEVEL` | Lower iot-client alone (lower-only, see the log section above) |
| `AGENTIC_KIT_TAI_LOG_LEVEL` | = `AGENTIC_KIT_LOG_LEVEL` | Lower rtc-tcp-client alone; below 3 the packet-log formatter goes too |
| `AGENTIC_KIT_TUYA_BLE_LOG_LEVEL` | = `AGENTIC_KIT_LOG_LEVEL` | Lower tuya-ble alone; `TUYA_BLE_HAL_LOGI` dispatches at debug, keeping it takes a 4 |

### iot-client: MQTT {#iot-client-mqtt}

| Configuration macro | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_MQTT_MAX_PACKET_SIZE` | 4096 | When a single CONNECT/SUBSCRIBE/PUBLISH packet overflows. **Coupling**: the DP publish gate `DP_MQTT_MAX_PAYLOAD` in `iot_dp.c` derives from it and follows automatically |
| `AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS` | 2000U | Weak networks, large-packet send timeouts |
| `AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS` | 1000U | Lower for a more responsive processing loop, raise to save wakeups |
| `AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS` | 10000U | Connection handshake budget on weak networks |

### iot-client: ATOP over HTTP {#iot-client-atop-over-http}

| Configuration macro | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE` | 1024 | When assembled request headers (request line + headers) no longer fit |
| `AGENTIC_KIT_RESPONSE_BUFFER_SIZE` | 4096 | **Must-read for products with a large DP schema**: the status line, response headers, and encrypted response body share this one buffer; overflow reports `OPRT_COMMUNICATION_ERROR`. The decrypted-JSON cap is roughly 2.8 KB net of headers (see [Generic ATOP Calls](./atop-generic-call)) |

### rtc-tcp-client (TAI 2.1) {#rtc-tcp-client-tai-21}

| Configuration macro | Default | When to adjust |
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

| Configuration macro | Default | When to adjust |
|------|------|---------|
| `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` | 6144 | **The unit is StackType_t words, not bytes** (6144 ≈ 24 KB on a 32-bit platform; the TLS handshake runs on this task — don't cut it to 6 KB by mistake) |
| `AGENTIC_KIT_PAL_FR_TASK_PRIORITY` | `tskIDLE_PRIORITY + 5` | Relative to the audio task and the application's main tasks |
| `AGENTIC_KIT_PAL_FR_TASK_NAME` | `"tai_worker"` | Debug display only |

## Migrating from the Old Names {#从旧名字迁移}

All configuration options now carry the `AGENTIC_KIT_` prefix (the collision backstory is above). A mechanical rename of old `-D` flags and override headers is all it takes:

| Old name | New name |
|------|------|
| `LOG_LEVEL` | `AGENTIC_KIT_LOG_LEVEL` |
| `TAI_LOG_LEVEL` | `AGENTIC_KIT_TAI_LOG_LEVEL` (still scoped to rtc-tcp-client, and lower-only below the SDK-wide ceiling; for the whole SDK use `AGENTIC_KIT_LOG_LEVEL`) |
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
- **`TUYA_BLE_RX_BUF_SIZE` / `TUYA_BLE_TX_BUF_SIZE` / `TUYA_BLE_TX_QUEUE_DEPTH`** — also in `tuya_ble_prov.h`, but for a different reason: they determine the layout of the public struct `tuya_ble_prov_state_t`, and ports size their own buffers against them, which makes them part of the port API; changing them changes a struct layout that ports must recompile and re-size against (the header itself declares the layout not ABI-stable) — they are not build configuration options. tuya-ble therefore has exactly one compile-time configuration option — the module log ceiling `AGENTIC_KIT_TUYA_BLE_LOG_LEVEL`, living in `modules/tuya-ble/include/tuya_ble_config_defaults.h` under the naming convention above (the full story on the geometry/layout constants sits in the `tuya_ble_prov.h` header comment).
- **`IOT_SDK_SW_VER` / `PV` / `BV` and the per-region ATOP hosts** — `modules/iot-client/src/iot_internal.h`; release-managed values, not build configuration options (an internal header, split from the configuration options, not riding the override mechanism).
- **coreMQTT / coreHTTP log routing** — `common/core_mqtt_config.h`, `common/core_http_config.h`; the routed log lines still pass the `AGENTIC_KIT_LOG_LEVEL` gate, nothing to tune separately.

## How to Confirm an Override Took Effect {#怎么确认覆盖生效了}

**Compile probe** (the most direct). With **exactly the same include paths and `-D`s as the real build** (if Option 2/3 used `-D`, the probe needs them too), compile this small file — an `#error` means it did not take:

```c
/* probe.c — include the defaults file that defines the configuration option (all of them pick up your override first) */
#include "iot_client_config_defaults.h"
#if AGENTIC_KIT_RESPONSE_BUFFER_SIZE != 8192
#error "override not picked up"
#endif
```

```sh
cc -I<sdk>/modules/iot-client/include -I<sdk>/common -I<your config dir> -c probe.c   # quiet pass = effective
```

**Behavioral observation.** When an ATOP response overflows, the error log states the current buffer size and suggests the matching `-D` (the default output shape is `HH:MM:SS [E] [module tag]`; `(server said …)` is the HTTP status — the server usually already returned 200 successfully, which is exactly the misunderstanding this line exists to clear):

```text
14:13:15 [E] [iot] HTTP response does not fit: need 6558 B body + 300 B headers, buffer is 4096 B (server said 200). Rebuild with a larger -DAGENTIC_KIT_RESPONSE_BUFFER_SIZE.
```

**Artifact inspection.** After lowering the log level, lines of the removed level stop printing; to confirm DEBUG strings never entered the firmware, search the artifact for a DEBUG message you recognize (`strings <library or object file> | grep '<that message>'` should come up empty). Note that a tag prefix like `[ble]` spans error/warn/debug levels and cannot by itself identify DEBUG output.
