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

> **注意：** PAL 只需提供原始 TCP 操作。TLS 由 SDK 内部通过 mbedTLS 处理（`tai_tls.c`），无需在 PAL 中实现 TLS 相关接口。

## 移植步骤

### 1. 实现 PAL 接口

创建一个 `pal_xxx.c` 文件，实现所有 PAL 函数并填充 `pal_t` 结构体：

```c
#include "pal.h"

static void *my_tcp_connect(const char *host, uint16_t port) {
    // 平台相关的 TCP 连接实现
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
| 互斥锁 | `xSemaphoreCreateMutex` |
| 时间 | `xTaskGetTickCount() * portTICK_PERIOD_MS` |

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
