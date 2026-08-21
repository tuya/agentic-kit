---
title: 固件 OTA 升级
sidebar_label: OTA 升级
sidebar_position: 6
---

# 固件 OTA 升级

本指南说明如何使用 agentic-kit 的 `iot_ota` API 实现设备固件 OTA（Over-The-Air）升级。

SDK 只提供**云端协议原语**——版本上报、升级查询、状态回报，以及 APP 确认通知回调；**固件的下载与烧写由应用负责**（例如 ESP-IDF 的 `esp_ota_*` 或厂商自有的 bootloader API）。完整主动查询示例见 `examples/esp-idf/ota-demo`。

如果产品要求“用户在 APP 上确认后才允许升级”，请将云端 OTA 任务配置为 APP 确认模式，并在设备上注册 `ota_confirm_callback`。agentic-kit 不调用 `tuya.device.upgrade.silent.get`，因此不会主动拉取或执行静默升级任务。

## 工作原理

```
设备启动 ──> iot_client_init (自动上报当前版本)
                │
                v
        iot_ota_check_upgrade() ──> 云端返回升级信息 (URL / 版本 / 大小 / 哈希)
                │
          有升级?
         /      \
       否        是
       │         │
   保持运行     iot_ota_report_status(UPGRADING)
                    │
                    v
              下载固件 (info.url) + 烧写 flash   ← 应用实现
                    │
              ┌─────┴─────┐
            成功          失败
              │            │
  report_status(FINI)   report_status(EXEC)
              │            │
          重启生效      重试 / 放弃
```

## 三个 API

| API | 云端接口 | 用途 |
|-----|---------|------|
| `iot_ota_report_version` | `tuya.device.versions.update` (v4.1) | 上报当前固件版本（`iot_client_init` 会用 `iot_client_config_t.sw_ver` 自动调用，NULL 时用 SDK 默认 `IOT_SDK_SW_VER`） |
| `iot_ota_check_upgrade` | `tuya.device.upgrade.get` (v4.4) | 查询是否有待升级固件，返回 URL / 版本 / 大小 / 哈希（云端与已上报的版本比较，不再传版本号） |
| `iot_ota_report_status` | `tuya.device.upgrade.status.update` (v4.1) | 回报升级生命周期状态 |
| `iot_ota_verify_init/update/finish` | — | 流式校验下载固件的 md5/hmac 摘要（见下文） |

## APP 确认后触发升级

APP 确认升级后，云端通过 MQTT 协议号 `15` 通知设备。SDK 解密后读取 `data.firmwareType` 作为固件 channel，并调用 `ota_confirm_callback`：

```c
static volatile bool g_ota_confirmed;
static volatile int g_ota_channel;

static void on_ota_confirmed(int channel, void *user_data)
{
    (void)user_data;
    g_ota_confirmed = true;   /* 只做轻量通知，不阻塞 MQTT process 线程 */
    g_ota_channel = channel;
}

iot_client_config_t cfg = {
    /* ... */
    .ota_confirm_callback = on_ota_confirmed,
    .ota_confirm_user_data = NULL,
};
```

应用主循环或专用 OTA 工作线程收到该信号后，再执行升级原语：

```text
APP 点击确认 ──> 云端下发 MQTT protocol 15
                     │
                     v
        ota_confirm_callback(channel)          /* SDK 只通知，不升级 */
                     │
              应用 worker 唤醒
                     │
                     v
        iot_ota_check_upgrade(client, channel, &info)
                     │
                 有升级？
                 /     \
               否       是
               │        │
           保持运行   report_status(UPGRADING)
                         │
                         v
                  下载 + 校验 + 烧写          /* 应用实现 */
                         │
                成功 / 失败
                  │       │
        report_status    report_status
          (COMPLETE)       (ERROR)
```

```c
iot_ota_upgrade_info_t info = {0};
if (!g_ota_confirmed) {
    /* 等待确认信号 */
}

int rc = iot_ota_check_upgrade(client, g_ota_channel, &info);
if (rc == OPRT_OK && info.has_upgrade) {
    rc = iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRADING);
    /* 在应用线程执行下载、iot_ota_verify_* 校验和平台 OTA 烧写 */
}
iot_ota_upgrade_info_free(client, &info);
```

`ota_confirm_callback` 与 `message_callback` 一样运行在调用 `iot_client_process()` / `iot_client_message_process()` 的线程内，coreMQTT 回调返回后还要继续处理 ack 和网络缓冲。回调中只允许置位标志、释放信号量或投递工作项；不要调用 `iot_ota_check_upgrade()`、下载固件、写 flash，也不要断开或销毁 IoT client。未注册该回调时，protocol 15 会继续透传给 `message_callback`，兼容旧应用自行解析的用法。

### `iot_ota_check_upgrade` 返回的升级信息

```c
typedef struct {
    bool  has_upgrade;   // 云端是否有升级
    char *version;       // 目标版本号
    char *url;           // 固件下载 URL（优先 cdnUrl，回退 httpsUrl）
    long  file_size;     // 固件大小（字节）
    int   channel;       // 固件通道（0 = 主 MCU）
    char *md5;           // MD5 校验（可能为 NULL）
    char *hmac;          // HMAC 校验（可能为 NULL）
} iot_ota_upgrade_info_t;
```

> 字段为堆分配，用完必须调 `iot_ota_upgrade_info_free()` 释放。

### 升级状态枚举

```c
typedef enum {
    OTA_STATUS_IDLE      = 0,  // 默认，不需要升级
    OTA_STATUS_READY     = 1,  // 设备准备就绪（升级任务已下发）
    OTA_STATUS_UPGRADING = 2,  // 升级中（下载/烧写前）
    OTA_STATUS_COMPLETE  = 3,  // 升级成功（重启前回报）
    OTA_STATUS_ERROR     = 4,  // 升级失败 / 异常
} iot_ota_status_t;
```

## 固件摘要校验（md5 / hmac）

云端在升级信息里返回固件摘要（`info.hmac` 优先，否则 `info.md5`；字段缺失或为空串都算没有下发该摘要）。SDK 提供**流式校验 API**：应用在下载循环中把每个固件块喂给校验器，下载完成后 `iot_ota_verify_finish()` 比对云端摘要，不匹配返回 `OPRT_OTA_VERIFY_FAILED`。

算法与 TuyaOpen 一致：

```
expected = HMAC-SHA256(key = 设备 secret_key,
                       msg = UPPERCASE_hex(SHA-256(固件字节)))
```

注意 HMAC 的消息是 SHA-256 摘要的 **64 字符大写十六进制字符串**（与 TuyaOpen 的 `hex2str` 一致，不是小写，也不是原始 32 字节）。云端没有下发 `hmac`（字段缺失或为空串）时退化为 `MD5(固件字节)` 比对；但 `hmac` 非空而长度不对时 `init` 直接报错，不会降级到 `md5`。比较大小写不敏感。

```c
iot_ota_verify_ctx_t *ctx = NULL;
int rc = iot_ota_verify_init(iot, &info, &ctx);
if (rc != OPRT_OK && rc != OPRT_NOT_SUPPORTED) {
    /* 摘要格式非法、内存不足等 —— 中止升级，上报 OTA_STATUS_ERROR */
    return -1;
}
/* rc == OPRT_NOT_SUPPORTED: 云端没有摘要字段，ctx 保持 NULL，跳过校验
 * （应用自行决定） */

while ((n = read_firmware_chunk(buf)) > 0) {
    if (ctx != NULL && iot_ota_verify_update(ctx, buf, n) != OPRT_OK) {
        iot_ota_verify_abort(ctx);
        return -1;
    }
    flash_write(buf, n);                  /* 与喂给校验器的数据一致 */
}

if (ctx != NULL) {
    rc = iot_ota_verify_finish(ctx);      /* 内部释放 ctx */
    if (rc != OPRT_OK) {
        /* OPRT_OTA_VERIFY_FAILED: 固件被篡改/损坏 —— 中止，上报 OTA_STATUS_ERROR */
        return -1;
    }
}
```

- 校验失败时**不要切换启动分区**，上报 `OTA_STATUS_ERROR` 后丢弃本次下载。
- 下载中途失败用 `iot_ota_verify_abort(ctx)` 直接释放（不比对）。
- `finish` 无论成败都会释放上下文，之后不要再使用；`finish(NULL)` 不是"跳过校验"，会返回参数错误，所以跳过校验的路径必须用 `if (ctx != NULL)` 把 `update`/`finish` 一起圈起来。
- `init` 只要返回**非 `OPRT_OK` 且非 `OPRT_NOT_SUPPORTED`** 就必须中止升级，`ctx` 此时不会被写入（保持 NULL）；按这两个值分支，不要枚举具体错误码。

## 完整示例（ESP-IDF）

以下步骤摘自 `examples/esp-idf/ota-demo/main/main.c`，使用 `esp_http_client` 下载、`esp_ota_*` 烧写。

### 1. 分区表

OTA 需要两个 app 分区（`ota_0` / `ota_1`）和一个 `otadata` 分区。demo 使用的 `partitions.csv`（16MB flash，每个 app 分区 4MB，可容纳约 4MB 的固件）：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
otadata,  data, ota,     0x10000, 0x2000,
ota_0,    app,  ota_0,   0x20000, 4M,
ota_1,    app,  ota_1,   ,        4M,
```

### 2. sdkconfig 关键项

```ini
# 给 TLS + HTTP + esp_ota 留够栈
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
# 16MB flash（容纳双 4MB OTA 分区）
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
# 自定义分区表
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
# 启用公共 CA 证书包（cdnUrl 下载需要）
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

### 3. 初始化与升级查询

```c
#include "iot_client.h"
#include "iot_ota.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"

const esp_app_desc_t *desc = esp_app_get_description();

iot_client_config_t iot_cfg = {
    .devid      = DEFAULT_DEVID,
    .secret_key = DEFAULT_SECRET_KEY,
    .local_key  = DEFAULT_LOCAL_KEY,
    .region     = DEFAULT_REGION,
    .env        = DEFAULT_ENV,
    /* mqtt_auto_connect = false: 只用 ATOP HTTP，不连 MQTT */
    .mqtt_auto_connect = false,
    /* 应用固件版本：init 时自动上报，供云端 OTA 比较（NULL 用 SDK 默认） */
    .sw_ver     = desc->version,
    /* 公共 CA 证书包：ATOP HTTPS（版本上报/升级查询/状态回报）需要 */
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};

/* iot_init(pal) 必须在 iot_client_init 前调用，否则 iot_client_init 返回 NULL */
iot_init(tai_pal_freertos());

iot_client_t *iot = iot_client_init(&iot_cfg);

/* 查询升级（云端与 init 时上报的 sw_ver 比较，无需再传版本号） */
iot_ota_upgrade_info_t info = {0};
int rc = iot_ota_check_upgrade(iot, 0, &info);
if (rc == OPRT_OK && info.has_upgrade) {
    ESP_LOGI(TAG, "upgrade -> %s  url=%s  size=%ld",
             info.version, info.url, info.file_size);
}
```

### 4. 上报状态、下载、烧写

```c
/* 下载前上报"升级中" */
iot_ota_report_status(iot, 0, OTA_STATUS_UPGRADING);

/* 用 esp_http_client 下载 info.url，逐块 esp_ota_write + 摘要校验 */
esp_err_t err = download_and_flash(iot, &info);
iot_ota_upgrade_info_free(iot, &info);

if (err != ESP_OK) {
    iot_ota_report_status(iot, 0, OTA_STATUS_ERROR);
    return;
}

/* 成功后上报"完成"，然后重启 */
iot_ota_report_status(iot, 0, OTA_STATUS_COMPLETE);
esp_restart();
```

`download_and_flash` 的核心流程（完整代码见 demo）：

```c
static esp_err_t download_and_flash(iot_client_t *iot,
                                    const iot_ota_upgrade_info_t *info)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);

    /* 摘要校验上下文（云端无 md5/hmac 时返回 OPRT_NOT_SUPPORTED，verify 保持 NULL） */
    iot_ota_verify_ctx_t *verify = NULL;
    int vrc = iot_ota_verify_init(iot, info, &verify);
    if (vrc != OPRT_OK && vrc != OPRT_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "iot_ota_verify_init failed: %d", vrc);
        return ESP_FAIL;   /* 校验器建不起来 —— 不装这个固件 */
    }

    esp_http_client_config_t http_cfg = {
        .url              = info->url,
        .timeout_ms       = 30000,
        .buffer_size      = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* 公共 CA 包 */
    };
    /* ... open / fetch headers / 检查 200；任何失败路径都要 iot_ota_verify_abort(verify) ... */

    esp_ota_handle_t handle;
    esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);

    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        if (verify != NULL) {
            iot_ota_verify_update(verify, (const uint8_t *)buf, n);  /* 与写入数据一致 */
        }
        esp_ota_write(handle, buf, n);                               /* 逐块写入 */
    }

    /* 切分区前校验云端摘要；不匹配则 esp_ota_abort 放弃 */
    if (verify != NULL) {
        vrc = iot_ota_verify_finish(verify);   /* 内部释放 verify */
        if (vrc != OPRT_OK) {
            ESP_LOGE(TAG, "Firmware digest mismatch (rc=%d)", vrc);
            esp_ota_abort(handle);
            return ESP_FAIL;
        }
    }

    esp_ota_end(handle);
    esp_ota_set_boot_partition(part);    /* 切换启动分区 */
    return ESP_OK;
}
```

### 5. 首次启动验证（防回滚）

重启后，新的固件应当把自己标记为有效，否则 ESP-IDF 会在若干次重启后回滚到旧分区：

```c
static void mark_current_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
}
```

在 `app_main` 开头调用一次即可。

## 构建与烧写

```bash
cd examples/esp-idf/ota-demo
idf set-target esp32s3
idf build
idf flash monitor
```

首次烧写会写到 `ota_0`；后续 OTA 写入 `ota_1` 并切换启动。

## 注意事项

- **SDK 不下载/不烧写**——`iot_ota` 只负责云端协议；下载校验、分区管理、防回滚全部由应用实现。
- **APP 确认模式**——云端任务需配置为 APP 确认模式；设备侧通过 `ota_confirm_callback` 接收 protocol 15，再由应用 worker 查询并执行升级。SDK 不调用静默升级接口。
- **栈要足够大**——TLS 握手 + HTTP 缓冲 + `esp_ota_write` 需要较大栈空间（demo 用 16KB）。
- **回报时机**——`UPGRADING` 在下载前、`COMPLETE` 在重启前、`ERROR` 在失败时；漏报会导致云端升级面板状态不准。
- **MD5/HMAC 摘要校验**——`iot_ota_verify_init/update/finish` 在下载时流式计算摘要，`esp_ota_set_boot_partition` **之前**完成校验；不匹配必须中止升级（见上文「固件摘要校验」）。
