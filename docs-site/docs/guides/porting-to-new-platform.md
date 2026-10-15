---
title: 适配新平台
sidebar_label: 适配新平台
sidebar_position: 3
---

# 适配新平台

RTC TCP Client 通过 PAL（Platform Abstraction Layer）实现跨平台移植。将 SDK 移植到新平台只需实现 `pal.h` 中定义的接口。

## PAL 接口总览 {#pal-接口总览}

| 接口类别 | 函数 | 说明 |
|----------|------|------|
| TCP | `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`, `tcp_poll` | TCP 连接管理、收发与轮询 |
| 线程 | `thread_create`, `thread_join` | 后台接收线程 |
| 互斥锁 | `mutex_create`, `mutex_lock`, `mutex_unlock`, `mutex_destroy` | 线程同步 |
| 时间 | `time_ms`, `wal_clock_time` | 单调毫秒计时用于超时；Unix UTC 毫秒时间戳 |
| 内存 | `malloc`, `free` | 动态内存分配 |

> **注意：** PAL 只需实现原始 TCP（`tcp_*`）、轮询（`tcp_poll`）、互斥锁（`mutex_*`）和时间（`wal_clock_time`、`time_ms`）等接口，无需在 PAL 中实现 TLS。TLS 现位于共享库 `common/tls.c`（TLS-over-TCP，被 iot-client 的 mqtt/http 与 rtc-tcp-client 共用；rtc-tcp-client 旧的 `src/tai_tls.c` 已删除）。TLS 握手在内部复用 PAL 的 `tcp_poll` / `mutex_*` / `time_ms`，因此这些 PAL 接口必须正确实现。

### 随机数 (RNG) {#随机数-rng}

`common/rng.c` 是进程级唯一的 CTR-DRBG，在启动时由 `tai_ctx_init()`（以及 iot-client 的初始化）调用 `rng_init()` 仅播种一次。它**要求** PAL 提供 `mutex_create`（见 `rng.c:51`）：若 PAL 缺少 `mutex_create`，`rng_init()` 会直接失败（fail closed），整个初始化随之失败。

## 移植步骤 {#移植步骤}

### 1. 实现 PAL 接口 {#1-实现-pal-接口}

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

### 2. 传入配置 {#2-传入配置}

```c
tai_config_t cfg = {
    .pal            = &my_platform_pal,
    // ... 其他配置
};
```

### 3. 编译集成 {#3-编译集成}

将 `modules/rtc-tcp-client/src/` 下的源文件和你的 PAL 实现一起编译即可。

## 平台实现参考 {#平台实现参考}

### ESP-IDF {#esp-idf}

| PAL 接口 | ESP-IDF 实现 |
|----------|-------------|
| TCP | lwIP socket API |
| 线程 | `xTaskCreate` / `vTaskDelete` |
| 互斥锁 | `xSemaphoreCreateRecursiveMutex` |
| 时间 | `time_ms`：FreeRTOS tick；`wal_clock_time`：libc UTC |
| 内存 | `pvPortMalloc` / `vPortFree` |

现成实现参考：`pal/pal_freertos.c` 和 `examples/esp-idf/components/agentic_kit/`

### Linux / macOS (POSIX) {#linux--macos-posix}

| PAL 接口 | POSIX 实现 |
|----------|-----------|
| TCP | `socket` / `connect` / `send` / `recv` |
| 线程 | `pthread_create` / `pthread_join` |
| 互斥锁 | `pthread_mutex_*` |
| 时间 | `time_ms`：`CLOCK_MONOTONIC`；`wal_clock_time`：`CLOCK_REALTIME`（均通过 `clock_gettime`） |
| 内存 | `malloc` / `free` |

现成实现参考：`pal/pal_posix.c`

### FreeRTOS（通用） {#freertos通用}

| PAL 接口 | FreeRTOS 实现 |
|----------|--------------|
| 线程 | `xTaskCreate` |
| 互斥锁 | `xSemaphoreCreateRecursiveMutex` |
| 时间 | 实现 `time_ms`（单调计时）和 `wal_clock_time`（UTC），参考 `pal_freertos.c` |

> **注意：** `pal.h` 的 `mutex_create` 契约（见头文件注释）要求返回**递归锁**，因此必须使用 `xSemaphoreCreateRecursiveMutex`（配合 `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`），不能用非递归的 `xSemaphoreCreateMutex`，否则会在重入加锁路径上死锁。

TCP 部分取决于具体的网络协议栈（lwIP、AT 指令等）。

### FreeRTOS 单调计时 {#freertos-tick-clock}

FreeRTOS 计时实现位于 `pal/pal_freertos.c` 内部。应用通过 `pal_t` 的回调读取时间，无需包含独立的计时头文件：

```c
#include "pal.h"

static void measure_work(const pal_t *pal)
{
    uint64_t start_ms = pal->time_ms();
    /* 执行操作。 */
    uint64_t elapsed_ms = pal->time_ms() - start_ms;
    (void)elapsed_ms;
}
```

- `time_ms` 在调度器启动后的任务上下文中调用，不支持 ISR。重启内核后重新建立计时起点。
- 内部通过 `vTaskSetTimeOutState()` 读取 tick 和回绕次数，无需 tick hook 或计时任务；支持 16、32、64 位 `TickType_t`。快照一致性依赖 FreeRTOS V10.4.3/V11.1.0 的 `TimeOut_t` 布局和临界区实现，超时记账 API 不承诺稳定的扩展时钟语义；升级内核或使用厂商分支时需复核。16/32 位 tick 要求至少 32 位 `UBaseType_t`，且内核回绕计数仍可表示。
- 根据 `configTICK_RATE_HZ` 换算毫秒，支持非整毫秒 tick 和高于 1000 Hz 的频率；避免中间乘法溢出，超出 64 位毫秒范围时饱和为 `UINT64_MAX`。
- 分辨率由 tick 频率决定；Tickless idle 的睡眠时间取决于平台的内核 tick 补偿。

可通过 `cmake --build build --target check_freertos_pal` 复核官方头文件兼容性，或运行 `python3 tools/check_freertos_pal.py --cc cc`。脚本固定 V10.4.3/V11.1.0 的上游提交，检查五种 tick 位宽组合；它会下载头文件，只验证编译兼容性，不验证实际内核运行或临界区。离线检查可通过 `--kernels-dir` 指定两个固定版本的 Git 检出目录。

### FreeRTOS UTC 时钟 {#freertos-wall-clock}

云端协议直接调用 `pal->wal_clock_time()` 获取 UTC 毫秒，并在协议使用处通过 `(uint32_t)(pal->wal_clock_time() / 1000U)` 转成 Unix 秒；RTC 媒体时间戳继续使用 UTC 毫秒。

`time_ms` 保留单调计时语义；新增的 `wal_clock_time` 返回自 1970-01-01 UTC 起的毫秒数。两项均为必填。自定义 PAL 保留原 `time_ms` 的 tick/uptime 实现，为 `wal_clock_time` 提供已同步的 UTC，并重新编译 SDK 与调用方。

现有 `pal_freertos.c` 通过 `gettimeofday()` 读取 libc UTC，应用负责通过 RTC/SNTP 设置它。其他 FreeRTOS 移植需在自己的 PAL 中接入 UTC 源；没有硬件 RTC 时也可以按“同步 UTC 基准 + 单调经过时间”计算，基准的读写需保证线程安全。SDK 不联网校时，也不创建校时任务。

UTC 可向前或向后校正，不能用于计算超时。TLS、MQTT、RTC 的超时与 Keepalive 使用 `time_ms`；云端签名、DP 上报和 RTC 媒体时间戳使用 `wal_clock_time`。UTC 不加时区偏移，时区只在显示日期时处理。启用 TLS 证书时间校验时，还需让 libc/mbedTLS 使用同一 UTC 源。

## ESP-IDF 特殊注意事项 {#esp-idf-特殊注意事项}

### 内存规划 {#内存规划}

| 组件 | 内存需求 | 建议分配位置 |
|------|---------|-------------|
| `tai_ctx_size()` | 依赖编译期缓冲配置，默认约 38 KB（调大 `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` 等缓冲后会相应增长，配置项默认值见 `modules/rtc-tcp-client/include/tai_config_defaults.h`） | 若平台支持，可优先考虑大块外部内存（如 ESP32-S3 PSRAM） |
| TLS 工作区 | ~30 KB | PSRAM |
| 音频发送缓冲 | ~4-8 KB | 内部 SRAM |
| 音频接收缓冲 | ~8-16 KB | 内部 SRAM 或 PSRAM |
| FreeRTOS 任务栈 | SDK worker 任务默认 6144 words ≈ 24 KB（`AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS`，单位是字不是字节；TLS 握手在该任务上，勿按 4-8 KB 砍）；其他任务 ~4-8 KB | 内部 SRAM |

```c
void *mem = heap_caps_malloc(tai_ctx_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### sdkconfig 推荐 {#sdkconfig-推荐}

```ini
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_FREERTOS_HZ=1000
```

### 任务优先级 {#任务优先级}

| 任务 | 优先级 |
|------|--------|
| 音频采集/播放 | 高 (15-18) |
| RTC TCP Client 后台线程 | 中 (10-12) |
| 应用主逻辑 | 中 (5-8) |

## 通用注意事项 {#通用注意事项}

- SDK 全部编译期配置项（TAI 缓冲与调度、MQTT 超时与包大小、ATOP HTTP 缓冲、FreeRTOS 任务栈、日志级别）的默认值与说明按所属子系统存放：日志上限与集成方覆盖的统一挂载点在 `common/log.h`（SDK 每个编译单元都包含它），FreeRTOS 任务配置项在 `pal/pal_config_defaults.h`，模块配置项在各自 include/ 下——`modules/iot-client/include/iot_client_config_defaults.h`、`modules/rtc-tcp-client/include/tai_config_defaults.h`、`modules/tuya-ble/include/tuya_ble_config_defaults.h`（tuya-ble 目前仅一个配置项：模块日志上限）。按产品覆盖任选其一（见 `common/log.h` 头注释；完整说明见[编译期配置](./compile-time-knobs.md)）：自己创建 `agentic_kit_config.h`（只写要改的 `#define`，无论配置项属于哪个子系统都写在这一个文件里），把所在目录加进编译 SDK 源码的 target 的 include 路径（CMake：`target_include_directories(<target> PRIVATE <目录>)`）即可被自动捡起；沿用 `-D<宏>=<值>`；或用 `-DAGENTIC_KIT_USER_CONFIG='"my_opts.h"'` 指定任意文件名（无 `__has_include` 的工具链用这种）。注意：配置项值必须对每个编译 SDK 源码的 target 保持一致
- PAL `thread_create` 需要设置足够的栈大小；`pal_freertos.c` 默认使用 `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS`（6144 words，32 位平台上约 24KB，可按平台内存情况调小或调大）
- SDK 内部通过 mbedTLS 处理 TLS，需要正确的系统时间用于证书验证；若未提供 CA 证书，TLS 连接可能退化为不校验证书的模式
- `tcp_recv` 应支持阻塞/超时语义（后台线程会循环调用）
- `tcp_poll` 用于检查套接字的可读/可写状态，需正确实现 events 位掩码
- 如使用 Opus 编码，需额外集成 Opus 库
- 云端签名与 DP 时间戳读取 PAL 的 UTC 回调，无需 libc `time(NULL)`。

### PAL I/O 返回值契约 {#pal-io-返回值契约}

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
