---
title: 适配新平台
sidebar_label: 适配新平台
sidebar_position: 3
---

# 适配新平台

RTC TCP Client 通过 PAL（Platform Abstraction Layer）实现跨平台移植。将 SDK 移植到新平台只需实现 `pal.h` 中定义的接口。

## PAL 接口总览

| 接口类别 | 函数 | 说明 |
|----------|------|------|
| TCP | `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`, `tcp_poll` | TCP 连接管理、收发与轮询 |
| 线程 | `thread_create`, `thread_join` | 后台接收线程 |
| 互斥锁 | `mutex_create`, `mutex_lock`, `mutex_unlock`, `mutex_destroy` | 线程同步 |
| 时间 | `time_ms` | 获取毫秒时间戳 |
| 内存 | `malloc`, `free` | 动态内存分配 |

> **注意：** PAL 只需实现原始 TCP（`tcp_*`）、轮询（`tcp_poll`）、互斥锁（`mutex_*`）和时间（`time_ms`）等接口，无需在 PAL 中实现 TLS。TLS 现位于共享库 `common/tls.c`（TLS-over-TCP，被 iot-client 的 mqtt/http 与 rtc-tcp-client 共用；rtc-tcp-client 旧的 `src/tai_tls.c` 已删除）。TLS 握手在内部复用 PAL 的 `tcp_poll` / `mutex_*` / `time_ms`，因此这些 PAL 接口必须正确实现。

### 随机数 (RNG)

`common/rng.c` 是进程级唯一的 CTR-DRBG，在启动时由 `tai_ctx_init()`（以及 iot-client 的初始化）调用 `rng_init()` 仅播种一次。它**要求** PAL 提供 `mutex_create`（见 `rng.c:51`）：若 PAL 缺少 `mutex_create`，`rng_init()` 会直接失败（fail closed），整个初始化随之失败。

## 移植步骤

### 1. 实现 PAL 接口

创建一个 `pal_xxx.c` 文件，实现所有 PAL 函数并填充 `pal_t` 结构体：

```c
#include "pal.h"

static void *my_tcp_connect(const char *host, uint16_t port, uint32_t timeout_ms) {
    // 平台相关的 TCP 连接实现；timeout_ms 内需完成连接
    // （0 = 非阻塞单次尝试），超时或失败返回 NULL
    return NULL;
}

// ... 实现其他函数 ...

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

### 2. 传入配置

```c
tai_config_t cfg = {
    .pal = &my_platform_pal,
    // ... 其他配置
};
```

### 3. 编译集成

将 `modules/rtc-tcp-client/src/` 下的源文件和你的 PAL 实现一起编译即可。

## 平台实现参考

### ESP-IDF

| PAL 接口 | ESP-IDF 实现 |
|----------|-------------|
| TCP | lwIP socket API |
| 线程 | `xTaskCreate` / `vTaskDelete` |
| 互斥锁 | `xSemaphoreCreateRecursiveMutex` |
| 时间 | `xTaskGetTickCount() * portTICK_PERIOD_MS` |
| 内存 | `pvPortMalloc` / `vPortFree` |

现成实现参考：`pal/pal_freertos.c` 和 `examples/esp-idf/components/agentic_kit/`

### Linux / macOS (POSIX)

| PAL 接口 | POSIX 实现 |
|----------|-----------|
| TCP | `socket` / `connect` / `send` / `recv` |
| 线程 | `pthread_create` / `pthread_join` |
| 互斥锁 | `pthread_mutex_*` |
| 时间 | `clock_gettime(CLOCK_MONOTONIC)` |
| 内存 | `malloc` / `free` |

现成实现参考：`pal/pal_posix.c`

### FreeRTOS（通用）

| PAL 接口 | FreeRTOS 实现 |
|----------|--------------|
| 线程 | `xTaskCreate` |
| 互斥锁 | `xSemaphoreCreateRecursiveMutex` |
| 时间 | `xTaskGetTickCount() * portTICK_PERIOD_MS` |

> **注意：** `pal.h:91` 要求 `mutex_create` 返回**递归锁**，因此必须使用 `xSemaphoreCreateRecursiveMutex`（配合 `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`），不能用非递归的 `xSemaphoreCreateMutex`，否则会在重入加锁路径上死锁。

TCP 部分取决于具体的网络协议栈（lwIP、AT 指令等）。

## ESP-IDF 特殊注意事项

### 内存规划

| 组件 | 内存需求 | 建议分配位置 |
|------|---------|-------------|
| `tai_ctx_size()` | 依赖编译期缓冲配置，默认可达数百 KB | 若平台支持，可优先考虑大块外部内存（如 ESP32-S3 PSRAM） |
| TLS 工作区 | ~30 KB | PSRAM |
| 音频发送缓冲 | ~4-8 KB | 内部 SRAM |
| 音频接收缓冲 | ~8-16 KB | 内部 SRAM 或 PSRAM |
| FreeRTOS 任务栈 | ~4-8 KB per task | 内部 SRAM |

```c
void *mem = heap_caps_malloc(tai_ctx_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### sdkconfig 推荐

```ini
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_FREERTOS_HZ=1000
```

### 任务优先级

| 任务 | 优先级 |
|------|--------|
| 音频采集/播放 | 高 (15-18) |
| RTC TCP Client 后台线程 | 中 (10-12) |
| 应用主逻辑 | 中 (5-8) |

## 通用注意事项

- PAL `thread_create` 需要设置足够的栈大小；`pal_freertos.c` 默认使用 `PAL_FR_TASK_STACK_WORDS`（约 6KB，可按平台内存情况调小或调大）
- SDK 内部通过 mbedTLS 处理 TLS，需要正确的系统时间用于证书验证；若未提供 CA 证书，TLS 连接可能退化为不校验证书的模式
- `tcp_recv` 应支持阻塞/超时语义（后台线程会循环调用）
- `tcp_poll` 用于检查套接字的可读/可写状态，需正确实现 events 位掩码
- 如使用 Opus 编码，需额外集成 Opus 库

### PAL I/O 返回值契约

PAL 的 TCP 接口必须严格遵循以下返回值约定（见 `pal.h`），否则 SDK 无法正确区分超时、对端关闭与致命错误：

| 接口 | 返回值 | 含义 |
|------|--------|------|
| `tcp_send` / `tcp_recv` | `>0` | 实际发送 / 接收的字节数 |
| `tcp_send` / `tcp_recv` | `0` | 无数据；`tcp_recv` 返回 `0` 表示对端已关闭连接（EOF） |
| `tcp_send` / `tcp_recv` | `PAL_ERR_AGAIN` (`-7`) | 超时 / would-block，稍后重试，非致命 |
| `tcp_send` / `tcp_recv` | `PAL_ERR_NET` (`-3`) | 致命网络错误 |
| `tcp_poll` | `>0` | 就绪事件位掩码：`1`=可读，`2`=可写（可按位组合） |
| `tcp_poll` | `0` | 超时（无事件就绪） |
| `tcp_poll` | `<0` | 错误 |
