---
title: 编译期旋钮配置
sidebar_label: 编译期旋钮
sidebar_position: 9
---

# 编译期旋钮配置

SDK 的全部编译期旋钮——TAI 收发缓冲与调度、MQTT 超时与包大小、ATOP HTTP 缓冲、FreeRTOS 任务栈、日志级别——共 19 个，默认值按**所属子系统**存放：日志上限 `AGENTIC_KIT_LOG_LEVEL` 在 `common/log.h`（这个头同时承载集成方覆盖的统一挂载点，原因见下文）；FreeRTOS 任务旋钮在 `pal/pal_config_defaults.h`；模块旋钮在各自 include/ 目录——`modules/iot-client/include/iot_client_config_defaults.h`（MQTT + ATOP HTTP）、`modules/rtc-tcp-client/include/tai_config_defaults.h`（TAI 缓冲与调度）；tuya-ble 目前没有编译期旋钮（原因见下文速查表）。每个旋钮的完整说明（单位、联动、踩过的坑）在各自文件的注释里；本页讲怎么按产品覆盖它们、这套机制为什么长这样，并给出速查表。

## 三种覆盖方式（任选其一） {#三种覆盖方式任选其一}

### 方式一（推荐）：自建 `agentic_kit_config.h` {#方式一推荐自建-agentic_kit_configh}

只写要改的旋钮（普通 `#define`，不用 `#ifndef`），把文件所在目录放进**编译 SDK 源码的 target** 的 include 路径——SDK 编译每个源文件时会自动捡起它，不需要任何 `-D`。无论旋钮属于哪个子系统，都写在这**一个**文件里（`common/log.h` 的捡起逻辑会在所有 `#ifndef` 默认值之前应用它），不需要按模块建多个覆盖文件：

```c
/* agentic_kit_config.h —— 只写要改的，其余用 SDK 默认 */
#define AGENTIC_KIT_RESPONSE_BUFFER_SIZE  8192   /* 产品 DP schema 大 */
#define AGENTIC_KIT_TAI_FRAG_BUF_SIZE    16000U  /* ESP32 无 PSRAM：缩小收包缓冲 */
#define AGENTIC_KIT_LOG_LEVEL                2   /* 量产只留 error+warn；代价见下文"日志"一节 */
```

ESP-IDF 工程（SDK 以组件形式编译）：把文件放进项目任意目录，加一行让组件看得到它——

```cmake
# 放在本组件的 CMakeLists.txt 里；路径指向你放 agentic_kit_config.h 的目录
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../kit_opts")
```

普通 CMake 工程：

```sh
cmake -B build -DCMAKE_C_FLAGS="-I<config 所在目录>"
```

### 方式二：逐个 `-D` {#方式二逐个-d}

只动一两个旋钮、或想在 CI 里按矩阵切换时最省事：

```cmake
target_compile_definitions(my_sdk_target PRIVATE
    AGENTIC_KIT_RESPONSE_BUFFER_SIZE=8192
    AGENTIC_KIT_LOG_LEVEL=2)
```

### 方式三：`AGENTIC_KIT_USER_CONFIG` 指定任意文件名 {#方式三-agentic_kit_user_config-指定任意文件名}

工具链没有 `__has_include`（如旧版 armcc）时的兜底。注意宏值要**带引号再整体一层引号**，这是最常见的拼写错误：

```sh
-DAGENTIC_KIT_USER_CONFIG='"my_kit_opts.h"'   # 文件目录同样要在 include 路径上
```

设置了它就优先于方式一的探测（不会两个都包含）；同一个旋钮不要在两处重复定义。

## 一条铁律：对所有编译 SDK 源码的 target 保持一致 {#一条铁律对所有编译-sdk-源码的-target-保持一致}

旋钮是编译期常量，直接决定结构体布局和缓冲尺寸。**不同编译单元看到不同的值不会有链接错误**——`tai_ctx_size()` 按一套值算大小、任务按另一套值分配内存，越界是静默发生的。无论用哪种方式，都要让每一个编译 SDK 源码的 target（SDK 库、直接编进应用的 SDK 源文件、测试程序）看到同一份配置。

## 为什么这样设计 {#为什么这样设计}

**为什么默认值分散在各子系统、覆盖挂载点却只有一个。** 这些默认值原本散落在各调用点，缓冲大小实际是**产品属性**（schema 多大、有没有 PSRAM、音频帧长多少），选型时需要按内存预算逐项审；生产事故复盘时也要能一眼回答"这块内存是哪个旋钮、为什么是这个值"。现在默认值跟着所属子系统走——日志在 `common/log.h`、FreeRTOS 任务在 `pal/pal_config_defaults.h`、模块旋钮在各自 include/——审预算、做评审时对着所属文件即可。而集成方覆盖的**捡起逻辑**只存在于 `common/log.h` 一处：SDK 每个编译单元都包含这个头，各 `*_config_defaults.h` 也都先包含它，因此任何 `#ifndef` 默认值生效前，你的覆盖一定已经就位——一份 `agentic_kit_config.h` 打动全部 19 个旋钮，不需要按子系统拆多个覆盖文件。

**为什么都加 `AGENTIC_KIT_` 前缀。** 撞名不是假设出来的风险：coreMQTT 自带的 `core_mqtt_config_defaults.h` 定义了同名 `MQTT_SEND_TIMEOUT_MS`（默认 20000U），与 SDK 的 2000U 谁生效取决于包含顺序；`LOG_LEVEL` 也被多个平台 SDK 占用。前缀把这些名字搬进 SDK 自己的命名空间——你的 `-D` 不会再打到别人的宏，别人的也不会打到你的。

**为什么 SDK 文件叫 `_defaults.h`，把裸名 `agentic_kit_config.h` 留给你。** `#include "..."`（带引号）会先搜索包含者所在目录（SDK 的 `common/`），再搜 `-I` 路径。如果 SDK 自己占用 `agentic_kit_config.h` 这个名字，你放在任何 include 路径里的同名文件都永远先被 SDK 自己的文件挡住。把裸名预留给你，是 lwIP `lwipopts.h`、mbedTLS `mbedtls_config.h`、FreeRTOS `FreeRTOSConfig.h` 一脉相承的约定：**覆盖文件的名字归集成方所有**。

**为什么是 `#ifndef` 默认 + 先包含你的文件，而不是让你直接改 SDK 文件。** 你不碰 SDK 源文件，升级没有合并冲突；你的文件里没写的旋钮自动跟随 SDK 默认值。

## 日志：编译期闸门，单层 {#日志编译期闸门单层}

`AGENTIC_KIT_LOG_LEVEL` 是整个 SDK 唯一的日志开关，且只在编译期起作用：**0 = 全关，1 = error，2 = +warn，3 = +info，4 = +debug（默认）**。高于上限的日志在编译期整体消失——没有函数调用、不求值参数、格式字符串也不进固件（直接省 flash/RAM）；上限以下的日志无条件输出。**没有运行时级别，编译进什么就出什么。**

因此日志量是构建决策：量产固件用 `-DAGENTIC_KIT_LOG_LEVEL=2` 编译，error + warn 之外的一切（代码与字符串）都不进镜像；开发构建保持默认 4 拿到全部日志。运行时层已整体移除（`log_set_level()`/`log_get_level()`/`tai_set_log_level()` 均已删除），级别不再有第二个开关。**日志去哪儿同样是构建决策**：默认输出到 stderr；定义 `AGENTIC_KIT_LOG`（见下节）把每行分发进你自己的宏——需要"运行时收放"的场合（比如测试里捕获/静默），把模式做进你宏的目标函数即可，SDK 不持有任何日志状态。

> **迁移**：原来的 per-module `-DTAI_LOG_LEVEL=N` 已并入 `-DAGENTIC_KIT_LOG_LEVEL=N`（作用于**整个 SDK**，不只 rtc-tcp-client）。原来在启动时调 `log_set_level(N)` 的代码：删除调用，改用 `-DAGENTIC_KIT_LOG_LEVEL=N` 编译，或在你 `AGENTIC_KIT_LOG` 的目标里按 level 过滤。原来用 `log_set_handler()` 安装 handler 的代码：把那个函数变成 `AGENTIC_KIT_LOG` 的目标（它现在直接拿到 level 和裸 tag，连格式串里的 tag 前缀都不用再剥）。

### 改写日志分发：AGENTIC_KIT_LOG {#改写日志分发agentic_kit_log}

日志的去向没有运行时开关，它由你在编译期决定：在旋钮的同一个覆盖文件里定义 `AGENTIC_KIT_LOG`，SDK 的每行日志就分发进你自己的宏。这正是接宏式日志系统（ESP-IDF 的 `ESP_LOGx`、Zephyr 的 `LOG_*`）的路——宏对宏，没有 `va_list` 中转（`va_list` 没法转发给宏，凡走函数桥接的方案都得 `vsnprintf` 进缓冲再拿 `%s` 喂出去：多一次拷贝、多一个截断点、丢掉对方宏的格式检查）：

```c
/* agentic_kit_config.h —— 与旋钮同一个文件、同一个捡起机制 */
#define AGENTIC_KIT_LOG(level, tag, fmt, ...) \
    ESP_LOG_LEVEL_LOCAL(lvl_of(level), tag, fmt, ##__VA_ARGS__)
```

SDK 里所有日志宏（iot-client 的 `IOT_LOG*`、`TAI_LOG*`、`TUYA_BLE_HAL_LOG*`）都汇到 `log_tag_*`，再经 `AGENTIC_KIT_LOG` 分发——一个定义接管全部。你拿到的是 **level、裸 tag（`"iot"`/`"tai"`/`"ble"`）、printf 格式串与参数**：tag 是独立 token，结构化 sink 和按 tag 过滤从此可行。

两条规则，两个方向都成立：

- **闸门仍然在上**：被 `AGENTIC_KIT_LOG_LEVEL` 编译掉的行，改写救不回来（`log_tag_*` 在上限之上展开为 `((void)0)`）；
- **改写必须覆盖编译 SDK 源的每个目标**——与旋钮同一条铁律。没有运行时分发可以回退，也没有第二道开关：编译进什么，就打印什么。默认展开保留 `log_emit` 的 `format(printf)` 编译期检查；换成你自己的宏就不再有这层检查（除非你自己加回 attribute）。你的目标若是函数而不是宏，它可以调 `log_emit_valist()`（facade 的 `va_list` 入口，对应 `esp_log_writev` 的角色）复用默认 stderr 输出，不必重写格式化。

## 旋钮速查 {#旋钮速查}

默认值与详细理由以各 config 文件的注释为准；"何时调整"是最常见的场景提示。各表所在文件：全 SDK 日志表在 `common/log.h`；PAL 表在 `pal/pal_config_defaults.h`；iot-client 两表在 `modules/iot-client/include/iot_client_config_defaults.h`；TAI 表在 `modules/rtc-tcp-client/include/tai_config_defaults.h`。

### 全 SDK {#全-sdk}

| 旋钮 | 默认 | 何时调整 |
|------|------|---------|
| `AGENTIC_KIT_LOG_LEVEL` | 4（debug） | 量产降到 1–2（编译期单层，见上文） |

### iot-client：MQTT {#iot-client-mqtt}

| 旋钮 | 默认 | 何时调整 |
|------|------|---------|
| `AGENTIC_KIT_MQTT_MAX_PACKET_SIZE` | 4096 | 单个 CONNECT/SUBSCRIBE/PUBLISH 包超限时。**联动**：`iot_dp.c` 的 DP 上报门槛 `DP_MQTT_MAX_PAYLOAD` 直接由它派生，自动跟随 |
| `AGENTIC_KIT_MQTT_SEND_TIMEOUT_MS` | 2000U | 网络差、大包发送超时 |
| `AGENTIC_KIT_MQTT_RECV_TIMEOUT_MS` | 1000U | 调小让处理循环更勤，调大省唤醒 |
| `AGENTIC_KIT_MQTT_CONNECT_TIMEOUT_MS` | 10000U | 弱网环境连接握手预算 |

### iot-client：ATOP over HTTP {#iot-client-atop-over-http}

| 旋钮 | 默认 | 何时调整 |
|------|------|---------|
| `AGENTIC_KIT_REQUEST_HEADER_BUFFER_SIZE` | 1024 | 请求头（请求行 + 头部）拼不下的场景 |
| `AGENTIC_KIT_RESPONSE_BUFFER_SIZE` | 4096 | **DP schema 大的产品必看**：状态行 + 响应头 + 加密响应体共用这一个缓冲，溢出报 `OPRT_COMMUNICATION_ERROR`。解密后 JSON 上限约 2.8 KB（详见[通用 ATOP 调用](./atop-generic-call)） |

### rtc-tcp-client（TAI 2.1） {#rtc-tcp-client-tai-21}

| 旋钮 | 默认 | 何时调整 |
|------|------|---------|
| `AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD` | 4096U | 传输分片上限，同时在 ClientHello 里通告给服务端。缩小省 RX 内存、增多分片开销 |
| `AGENTIC_KIT_TAI_FRAG_BUF_SIZE` | 32000U | 重组缓冲 = 最大下行应用包上限（大 Event / MCP 命令 / context JSON）。无 PSRAM 的 ESP32 常调小 |
| `AGENTIC_KIT_TAI_TX_HDR_BUF_SIZE` | 256U | 发送侧 scatter-gather 头缓冲，一般不动 |
| `AGENTIC_KIT_TAI_FRAME_COALESCE_LIMIT` | 512U | 小帧合并阈值，调小只缩窄合并窗口，安全 |
| `AGENTIC_KIT_TAI_TX_CTRL_BUF_SIZE` | 1024U | 控制包组装缓冲，约 `2×strlen(session JSON) + 115`。session/event 配置丰富时抬到 2048/4096 |
| `AGENTIC_KIT_TAI_MAX_ATTRS` | 32 | 单包最大属性数，一般不动 |
| `AGENTIC_KIT_TAI_DRAIN_BUDGET_MS` | 150U | 收包 worker 单轮排水预算，影响洪泛下 keepalive/关停延迟 |
| `AGENTIC_KIT_TAI_WORKER_POLL_CAP_MS` | 2000U | worker 空闲阻塞上限，影响 `tai_disconnect()` 响应速度 |
| `AGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N` | 50 | 媒体中间帧 1/N 采样打日志；0 = 关采样（全部降为 DEBUG） |

### PAL：FreeRTOS 任务 {#pal-freertos-任务}

| 旋钮 | 默认 | 何时调整 |
|------|------|---------|
| `AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS` | 6144 | **单位是 StackType_t 字，不是字节**（32 位平台 6144 ≈ 24 KB；TLS 握手跑在这个任务上，别按 6 kB 误砍） |
| `AGENTIC_KIT_PAL_FR_TASK_PRIORITY` | `tskIDLE_PRIORITY + 5` | 与音频任务、应用主任务的相对优先级 |
| `AGENTIC_KIT_PAL_FR_TASK_NAME` | `"tai_worker"` | 仅调试显示 |

## 从旧名字迁移 {#从旧名字迁移}

所有旋钮已统一加 `AGENTIC_KIT_` 前缀（撞名背景见上文）。旧的 `-D` 与覆盖头文件做机械改名即可：

| 旧名 | 新名 |
|------|------|
| `LOG_LEVEL` | `AGENTIC_KIT_LOG_LEVEL` |
| `TAI_LOG_LEVEL` | 并入 `AGENTIC_KIT_LOG_LEVEL`（现作用于整个 SDK） |
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

## 这些名字故意不在 config 文件里 {#这些名字故意不在-config-文件里}

- **`TUYA_BLE_HAL_LOGI/LOGW/LOGE/HEXDUMP`** —— 定义在 `modules/tuya-ble/include/tuya_ble_prov.h`，是该公开头文件的端口绑定契约，由端口在包含前覆盖。
- **`TUYA_BLE_RX_BUF_SIZE` / `TUYA_BLE_TX_BUF_SIZE` / `TUYA_BLE_TX_QUEUE_DEPTH`** —— 同样在 `tuya_ble_prov.h`，但原因不同：它们决定公共结构体 `tuya_ble_prov_state_t` 的布局，端口按它们设定自己的缓冲尺寸，属于端口 API 的一部分；改它们改的是端口要跟着重编、重定尺寸的结构体布局（该头文件本身声明布局不保证 ABI 稳定），不是构建旋钮。tuya-ble 目前因此没有任何编译期旋钮（完整说明见 `tuya_ble_prov.h` 的头注释；将来出现旋钮时将以 `AGENTIC_KIT_TUYA_BLE_*` 命名落在 `modules/tuya-ble/include/tuya_ble_config_defaults.h`）。
- **`IOT_SDK_SW_VER` / `PV` / `BV` 与区域 ATOP 域名** —— `modules/iot-client/src/iot_internal.h`，发版管理的值，不是构建旋钮（内部头，与旋钮分家，不走覆盖机制）。
- **coreMQTT / coreHTTP 日志路由** —— `common/core_mqtt_config.h`、`common/core_http_config.h`；路由后的日志行同样过 `AGENTIC_KIT_LOG_LEVEL` 闸门，无需单独调整。

## 怎么确认覆盖生效了 {#怎么确认覆盖生效了}

**编译探针**（最直接）。用**与正式构建完全相同的 include 路径和 `-D`**（方式二/三用了 `-D` 的话探针也要带上）编译这个小文件，报 `#error` 即未生效：

```c
/* probe.c —— 包含定义该旋钮的 defaults 文件（都会先捡起你的覆盖） */
#include "iot_client_config_defaults.h"
#if AGENTIC_KIT_RESPONSE_BUFFER_SIZE != 8192
#error "override not picked up"
#endif
```

```sh
cc -I<sdk>/modules/iot-client/include -I<sdk>/common -I<你的 config 目录> -c probe.c   # 安静通过 = 生效
```

**行为观察**。ATOP 响应超限时，错误日志会直接给出当前缓冲大小并建议对应 `-D`（默认输出的形状是 `HH:MM:SS [E] [模块tag]`；`(server said …)` 是 HTTP 状态码——服务端往往已成功返回 200，这正是这行日志要澄清的误解）：

```text
14:13:15 [E] [iot] HTTP response does not fit: need 6558 B body + 300 B headers, buffer is 4096 B (server said 200). Rebuild with a larger -DAGENTIC_KIT_RESPONSE_BUFFER_SIZE.
```

**产物检查**。日志级别调低后，对应级别的行不再输出；要确认 DEBUG 字符串没进固件，用一条你认识的 DEBUG 文案在产物里搜（`strings <库或对象文件> | grep '<那条文案>'` 应为空）。注意 `[ble]` 这类 tag 前缀横跨 error/warn/debug 三个级别，不能单独当作 DEBUG 的判据。
