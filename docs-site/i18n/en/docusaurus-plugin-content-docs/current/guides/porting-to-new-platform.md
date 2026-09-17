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
| Time | `time_ms` | Obtain a millisecond timestamp |
| Memory | `malloc`, `free` | Dynamic memory allocation |

> **Note:** The PAL only needs to implement raw TCP (`tcp_*`), polling (`tcp_poll`), mutex (`mutex_*`), and time (`time_ms`) interfaces. TLS does not need to be implemented in the PAL. TLS now resides in the shared library `common/tls.c` (TLS-over-TCP shared by mqtt/http in iot-client and rtc-tcp-client; the old rtc-tcp-client `src/tai_tls.c` has been removed). Internally, the TLS handshake reuses the PAL's `tcp_poll` / `mutex_*` / `time_ms`, so these PAL interfaces must be implemented correctly.

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
    .tcp_connect   = my_tcp_connect,
    .tcp_send      = my_tcp_send,
    .tcp_recv      = my_tcp_recv,
    .tcp_close     = my_tcp_close,
    .tcp_poll      = my_tcp_poll,
    .time_ms       = my_time_ms,
    .malloc        = my_malloc,
    .free          = my_free,
    .mutex_create  = my_mutex_create,
    .mutex_lock    = my_mutex_lock,
    .mutex_unlock  = my_mutex_unlock,
    .mutex_destroy = my_mutex_destroy,
    .thread_create = my_thread_create,
    .thread_join   = my_thread_join,
};
```

### 2. Pass the Configuration {#2-传入配置}

```c
tai_config_t cfg = {
    .pal = &my_platform_pal,
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
| Time | `xTaskGetTickCount() * portTICK_PERIOD_MS` |
| Memory | `pvPortMalloc` / `vPortFree` |

Reference implementations: `pal/pal_freertos.c` and `examples/esp-idf/components/agentic_kit/`

### Linux / macOS (POSIX) {#linux--macos-posix}

| PAL interface | POSIX implementation |
|----------|-----------|
| TCP | `socket` / `connect` / `send` / `recv` |
| Threads | `pthread_create` / `pthread_join` |
| Mutexes | `pthread_mutex_*` |
| Time | `clock_gettime(CLOCK_MONOTONIC)` |
| Memory | `malloc` / `free` |

Reference implementation: `pal/pal_posix.c`

### FreeRTOS (Generic) {#freertos通用}

| PAL interface | FreeRTOS implementation |
|----------|--------------|
| Threads | `xTaskCreate` |
| Mutexes | `xSemaphoreCreateRecursiveMutex` |
| Time | `xTaskGetTickCount() * portTICK_PERIOD_MS` |

> **Note:** The `mutex_create` contract in `pal.h` (see the header comment) requires a **recursive mutex**. Therefore, you must use `xSemaphoreCreateRecursiveMutex` with `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`, not the non-recursive `xSemaphoreCreateMutex`; otherwise, reentrant locking paths deadlock.

The TCP implementation depends on the specific network stack, such as lwIP or AT commands.

## ESP-IDF-Specific Considerations {#esp-idf-特殊注意事项}

### Memory Planning {#内存规划}

| Component | Memory requirement | Recommended location |
|------|---------|-------------|
| `tai_ctx_size()` | Depends on compile-time buffer configuration; approximately 38 KB by default (it grows if buffers such as `TAI_FRAG_BUF_SIZE` are increased) | Prefer large external memory, such as ESP32-S3 PSRAM, if the platform supports it |
| TLS workspace | ~30 KB | PSRAM |
| Audio send buffer | ~4-8 KB | Internal SRAM |
| Audio receive buffer | ~8-16 KB | Internal SRAM or PSRAM |
| FreeRTOS task stack | ~4-8 KB per task | Internal SRAM |

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

- PAL `thread_create` must configure a sufficiently large stack. `pal_freertos.c` uses `PAL_FR_TASK_STACK_WORDS` by default (6144 words, approximately 24 KB on a 32-bit platform), which can be reduced or increased according to the platform's memory constraints.
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
