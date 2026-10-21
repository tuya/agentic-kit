---
title: Port to a New Platform
sidebar_label: Port to a New Platform
sidebar_position: 3
---

# Port to a New Platform

The RTC TCP Client uses a PAL (Platform Abstraction Layer) for cross-platform portability. To port the SDK to a new platform, implement the interfaces defined in `pal.h`.

## PAL Interface Overview {#pal-接口总览}

| Interface category | Functions | Description |
|----------|------|------|
| TCP | `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`, `tcp_poll` | TCP connection management, sending, receiving, and polling |
| Threads | `thread_create`, `thread_join` | Background receive thread |
| Mutexes | `mutex_create`, `mutex_lock`, `mutex_unlock`, `mutex_destroy` | Thread synchronization |
| Time | `time_ms`, `wal_clock_time` | Monotonic milliseconds for deadlines; Unix UTC milliseconds |
| Memory | `malloc`, `free` | Dynamic memory allocation |

> **Note:** The PAL only needs to implement raw TCP (`tcp_*`), polling (`tcp_poll`), mutex (`mutex_*`), and time (`wal_clock_time`, `time_ms`) interfaces. TLS does not need to be implemented in the PAL. TLS now resides in the shared library `common/tls.c` (TLS-over-TCP shared by mqtt/http in iot-client and rtc-tcp-client; the old rtc-tcp-client `src/tai_tls.c` has been removed). Internally, the TLS handshake reuses the PAL's `tcp_poll` / `mutex_*` / `time_ms`, so these PAL interfaces must be implemented correctly.

### Random Number Generation (RNG) {#随机数-rng}

`common/rng.c` is the single process-wide CTR-DRBG. At startup, `tai_ctx_init()` (and iot-client initialization) calls `rng_init()` to seed it exactly once. It **requires** the PAL to provide `mutex_create` (see `rng.c:51`). If the PAL lacks `mutex_create`, `rng_init()` fails immediately (fail closed), causing the entire initialization to fail.

## Porting Steps {#移植步骤}

### 1. Implement the PAL Interfaces {#1-实现-pal-接口}

Create a `pal_xxx.c` file, implement every PAL function, and populate the `pal_t` struct:

```c
#include "pal.h"

static void *my_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms) {
    // Platform-specific TCP connection implementation. The connection must be
    // established within timeout_ms (0 = one non-blocking attempt). Return
    // NULL on timeout or failure.
    return NULL;
}

// ... Implement the other functions ...

const pal_t my_platform_pal = {
    .tcp_connect    = my_tcp_connect,
    .tcp_send       = my_tcp_send,
    .tcp_recv       = my_tcp_recv,
    .tcp_close      = my_tcp_close,
    .tcp_poll       = my_tcp_poll,
    .time_ms        = my_time_ms,
    .wal_clock_time = my_wal_clock_time,
    .malloc         = my_malloc,
    .free           = my_free,
    .mutex_create   = my_mutex_create,
    .mutex_lock     = my_mutex_lock,
    .mutex_unlock   = my_mutex_unlock,
    .mutex_destroy  = my_mutex_destroy,
    .thread_create  = my_thread_create,
    .thread_join    = my_thread_join,
    .sleep_ms       = my_sleep_ms,
};
```

### 2. Pass the Configuration {#2-传入配置}

```c
tai_config_t cfg = {
    .pal            = &my_platform_pal,
    // ... Other configuration
};
```

### 3. Integrate the Build {#3-编译集成}

Compile the source files under `modules/rtc-tcp-client/src/` together with your PAL implementation.

## Platform Implementation References {#平台实现参考}

### ESP-IDF {#esp-idf}

| PAL interface | ESP-IDF implementation |
|----------|-------------|
| TCP | lwIP socket API |
| Threads | `xTaskCreate` / `vTaskDelete` |
| Mutexes | `xSemaphoreCreateRecursiveMutex` |
| Time | `time_ms`: FreeRTOS ticks; `wal_clock_time`: libc `gettimeofday()` |
| Memory | `pvPortMalloc` / `vPortFree` |

Reference implementations: `pal/pal_freertos.c` and `examples/esp-idf/components/agentic_kit/`

### Linux / macOS (POSIX) {#linux--macos-posix}

| PAL interface | POSIX implementation |
|----------|-----------|
| TCP | `socket` / `connect` / `send` / `recv` |
| Threads | `pthread_create` / `pthread_join` |
| Mutexes | `pthread_mutex_*` |
| Time | `time_ms`: `CLOCK_MONOTONIC`; `wal_clock_time`: `CLOCK_REALTIME` (both via `clock_gettime`) |
| Memory | `malloc` / `free` |

Reference implementation: `pal/pal_posix.c`

### FreeRTOS (Generic) {#freertos通用}

| PAL interface | FreeRTOS implementation |
|----------|--------------|
| Threads | `xTaskCreate` |
| Mutexes | `xSemaphoreCreateRecursiveMutex` |
| Time | See `pal_freertos.c` for `time_ms`; `wal_clock_time` defaults to libc `gettimeofday()` and can be replaced for the platform |

> **Note:** The `mutex_create` contract in `pal.h` (see the header comment) requires a **recursive mutex**. Therefore, you must use `xSemaphoreCreateRecursiveMutex` with `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`, not the non-recursive `xSemaphoreCreateMutex`; otherwise, reentrant locking paths deadlock.

The TCP implementation depends on the specific network stack, such as lwIP or AT commands.

### FreeRTOS Monotonic Time {#freertos-tick-clock}

The FreeRTOS clock implementation is private to `pal/pal_freertos.c`. Applications read time through the `pal_t` callbacks, without a separate time header:

```c
#include "pal.h"

static void measure_work(const pal_t *pal)
{
    uint64_t start_ms = pal->time_ms();
    /* Perform work. */
    uint64_t elapsed_ms = pal->time_ms() - start_ms;
    (void)elapsed_ms;
}
```

- Call `time_ms` from task context after the scheduler starts, never from an ISR. Establish new timing origins after a kernel restart.
- Internally, `vTaskSetTimeOutState()` reads ticks and their overflow count without a tick hook or timing task. It supports 16/32/64-bit `TickType_t`. Snapshot consistency depends on the `TimeOut_t` layout and critical-section implementation in FreeRTOS V10.4.3/V11.1.0; the timeout bookkeeping API does not promise a stable extended clock. Recheck kernel upgrades and vendor forks. For 16/32-bit ticks, `UBaseType_t` must be at least 32 bits and the kernel overflow count must remain representable.
- Conversion uses `configTICK_RATE_HZ`, including fractional-millisecond ticks and rates above 1000 Hz. It avoids intermediate multiplication overflow and saturates at `UINT64_MAX` if the millisecond range is exhausted.
- Resolution follows the tick frequency; tickless-idle sleep time follows the port's kernel tick compensation.

Reproduce the official-header checks with `cmake --build build --target check_freertos_pal`, or `python3 tools/check_freertos_pal.py --cc cc`. The script pins upstream V10.4.3/V11.1.0 commits and checks five tick-width configurations. It downloads headers and verifies compilation only, not kernel execution or critical sections. For offline checks, `--kernels-dir` accepts git checkouts of both pinned versions.

### FreeRTOS UTC Clock {#freertos-wall-clock}

Cloud protocols call `pal->wal_clock_time()` directly for UTC milliseconds and convert to Unix seconds at the protocol call sites with `(uint32_t)(pal->wal_clock_time() / 1000U)`; RTC media timestamps continue using UTC milliseconds.

`time_ms` keeps its monotonic timing contract; the new `wal_clock_time` returns milliseconds since 1970-01-01 UTC. Both are mandatory. Custom PALs retain their existing tick/uptime implementation in `time_ms`, supply synchronized UTC in `wal_clock_time`, and rebuild the SDK and its consumers.

The default `pal_wal_clock_time()` in `pal_freertos.c` reads UTC through libc `gettimeofday()` and converts seconds and microseconds to milliseconds. It returns zero if the read fails or Unix seconds are non-positive. This implementation requires a platform/libc that provides `gettimeofday()`, such as the ESP-IDF libc/lwIP environment; the FreeRTOS kernel itself provides no UTC clock. The application maintains that clock through RTC/SNTP and can use the factory PAL directly when this source is suitable:

```c
const pal_t *pal = tai_pal_freertos();
if (iot_init(pal) != OPRT_OK) {
    return;
}
tai_config_t cfg = {
    .pal = pal,
    /* ... */
};
```

If the platform provides `gettimeofday()` but its UTC source is unsuitable, replace the callback before initialization. The application implements `app_utc_ms()` below to return Unix UTC milliseconds, or zero when time is unavailable:

```c
static pal_t app_pal;

/* Run once at startup, before the SDK or worker threads. */
app_pal = *tai_pal_freertos();
app_pal.wal_clock_time = app_utc_ms;
if (iot_init(&app_pal) != OPRT_OK) {
    return;
}
tai_config_t cfg = {
    .pal = &app_pal,
    /* ... */
};
```

Keep a custom PAL alive throughout SDK use, share it between IoT and TAI, and do not modify it after initialization. If the toolchain lacks `gettimeofday()`, adapt `pal_freertos.c` or supply a libc compatibility implementation; replacing a callback at runtime does not remove that compile/link dependency. A custom UTC source can read a hardware RTC directly. Without an RTC, it can compute synchronized UTC plus elapsed monotonic time, protecting shared anchor reads and updates.

The SDK performs no network synchronization or background clock work and never falls back to monotonic time. A positive reading does not prove synchronization has completed; the application must still establish valid UTC. The POSIX `CLOCK_REALTIME` implementation and default initialization behavior remain unchanged.

UTC may step forwards or backwards and must not be used for timeouts. TLS/MQTT/RTC deadlines and Keepalive use `time_ms`; cloud signing, DP reports and RTC media timestamps use `wal_clock_time`. Store UTC without a timezone offset; apply timezones only when displaying calendar dates. When certificate-time checks are enabled, libc/mbedTLS must use the same UTC source.

## ESP-IDF-Specific Considerations {#esp-idf-特殊注意事项}

### Memory Planning {#内存规划}

| Component | Memory requirement | Recommended location |
|------|---------|-------------|
| `tai_ctx_size()` | Depends on compile-time buffer configuration; approximately 38 KB by default (it grows when buffers such as `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` are raised — configuration option defaults live in `modules/rtc-tcp-client/include/tai_config_defaults.h`) | Prefer large external memory, such as ESP32-S3 PSRAM, if the platform supports it |
| TLS workspace | ~30 KB | PSRAM |
| Audio send buffer | ~4-8 KB | Internal SRAM |
| Audio receive buffer | ~8-16 KB | Internal SRAM or PSRAM |
| FreeRTOS task stack | The SDK worker task defaults to 6144 words ≈ 24 KB (`AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` — the unit is words, not bytes; the TLS handshake runs on this task, so do not cut it to 4-8 KB); other tasks ~4-8 KB | Internal SRAM |

```c
void *mem = heap_caps_malloc(tai_ctx_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### Recommended sdkconfig {#sdkconfig-推荐}

```ini
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_FREERTOS_HZ=1000
```

### Task Priorities {#任务优先级}

| Task | Priority |
|------|--------|
| Audio capture/playback | High (15-18) |
| RTC TCP Client background thread | Medium (10-12) |
| Main application logic | Medium (5-8) |

## General Considerations {#通用注意事项}

- Defaults and documentation for every SDK compile-time configuration option (TAI buffers and scheduling, MQTT timeouts and packet size, ATOP HTTP buffers, the FreeRTOS task stack, log levels) live with the subsystem that owns them: the log ceiling and the single integrator-override pickup live in `common/log.h` (every SDK translation unit includes it), the FreeRTOS task configuration options in `pal/pal_config_defaults.h`, and the per-module configuration options in each module's include/ directory — `modules/iot-client/include/iot_client_config_defaults.h`, `modules/rtc-tcp-client/include/tai_config_defaults.h`, `modules/tuya-ble/include/tuya_ble_config_defaults.h` (tuya-ble has exactly one configuration option today: its module log ceiling). To override per product, pick one (see the banner in `common/log.h`; the full guide is [Compile-Time Configuration](./compile-time-knobs.md)): create your own `agentic_kit_config.h` (only the `#define`s you want to change — one file regardless of which subsystem a configuration option belongs to), add its directory to the include path of every target that compiles SDK sources (CMake: `target_include_directories(<target> PRIVATE <dir>)`) and it is picked up automatically; or keep using `-D<MACRO>=<value>`; or use `-DAGENTIC_KIT_USER_CONFIG='"my_opts.h"'` to name an arbitrary file (for toolchains without `__has_include`). Note: configuration option values must stay identical for every target that compiles SDK sources
- PAL `thread_create` must configure a sufficiently large stack. `pal_freertos.c` defaults to `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` (6144 words, approximately 24 KB on a 32-bit platform), which can be reduced or increased according to the platform's memory constraints.
- The SDK handles TLS internally through mbedTLS and requires the correct system time for certificate verification. If no CA certificate is provided, the TLS connection may fall back to a mode that does not verify certificates.
- `tcp_recv` should support blocking and timeout semantics; the background thread calls it repeatedly.
- `tcp_poll` checks whether the socket is readable or writable and must implement the events bitmask correctly.
- If Opus encoding is used, integrate the Opus library separately.

### PAL I/O Return-Value Contract {#pal-io-返回值契约}

The PAL TCP interfaces must strictly follow the return-value conventions below (see `pal.h`), or the SDK cannot correctly distinguish a timeout, peer closure, and a fatal error:

| Interface | Return value | Meaning |
|------|--------|------|
| `tcp_send` / `tcp_recv` | `>0` | Number of bytes actually sent or received |
| `tcp_send` / `tcp_recv` | `0` | No data; `tcp_recv` returning `0` means the peer closed the connection (EOF) |
| `tcp_send` / `tcp_recv` | `PAL_ERR_AGAIN` (`-7`) | Timeout / would-block; retry later; non-fatal |
| `tcp_send` / `tcp_recv` | `PAL_ERR_NET` (`-3`) | Fatal network error |
| `tcp_poll` | `>0` | Ready-event bitmask: `1` = readable, `2` = writable (may be combined bitwise) |
| `tcp_poll` | `0` | Timeout (no events ready) |
| `tcp_poll` | `<0` | Error |
