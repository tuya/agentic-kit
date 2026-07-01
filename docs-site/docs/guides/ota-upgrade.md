---
title: 固件 OTA 升级
sidebar_label: OTA 升级
sidebar_position: 6
---

# 固件 OTA 升级

本指南说明如何使用 agentic-kit 的 `iot_ota` API 实现设备固件 OTA（Over-The-Air）升级。

SDK 只提供**云端协议原语**——版本上报、升级查询、状态回报；**固件的下载与烧写由应用负责**（例如 ESP-IDF 的 `esp_ota_*` 或厂商自有的 bootloader API）。完整示例见 `examples/esp-idf/ota-demo`。

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
| `iot_ota_report_version` | `tuya.device.versions.update` (v4.1) | 上报当前固件版本（`iot_client_init` 也会自动调用） |
| `iot_ota_check_upgrade` | `tuya.device.upgrade.get` (v4.4) | 查询是否有待升级固件，返回 URL / 版本 / 大小 / 哈希 |
| `iot_ota_report_status` | `tuya.device.upgrade.status.update` (v4.1) | 回报升级生命周期状态 |

### `iot_ota_check_upgrade` 返回的升级信息

```c
typedef struct {
    bool  has_upgrade;   // 云端是否有升级
    char *version;       // 目标版本号
    char *url;           // 固件下载 URL（HTTPS）
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
    OTA_STATUS_IDLE        = 0,  // 空闲
    OTA_STATUS_UPGRADING   = 1,  // 升级中（下载/烧写前）
    OTA_STATUS_UPGRAD_FINI = 2,  // 升级成功（通常重启后回报）
    OTA_STATUS_UPGRD_EXEC  = 3,  // 升级失败
    OTA_STATUS_UPGRD_ABORT = 4,  // 升级中止
} iot_ota_status_t;
```

## 完整示例（ESP-IDF）

以下步骤摘自 `examples/esp-idf/ota-demo/main/main.c`，使用 `esp_http_client` 下载、`esp_ota_*` 烧写。

### 1. 分区表

OTA 需要两个 app 分区（`ota_0` / `ota_1`）和一个 `otadata` 分区。demo 使用的 `partitions.csv`（8MB flash）：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
otadata,  data, ota,     0x10000, 0x2000,
ota_0,    app,  ota_0,   0x20000, 1500K,
ota_1,    app,  ota_1,   ,        1500K,
```

### 2. sdkconfig 关键项

```ini
# 给 TLS + HTTP + esp_ota 留够栈
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
# 8MB flash
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
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

const esp_app_desc_t *desc = esp_app_get_description();

iot_client_config_t iot_cfg = {
    .devid      = DEFAULT_DEVID,
    .secret_key = DEFAULT_SECRET_KEY,
    .local_key  = DEFAULT_LOCAL_KEY,
    .region     = DEFAULT_REGION,
    .env        = DEFAULT_ENV,
    /* mqtt_auto_connect = false: 只用 ATOP HTTP，不连 MQTT */
    .mqtt_auto_connect = false,
};
iot_client_t *iot = iot_client_init(&iot_cfg);

/* 查询升级，传入当前版本号供云端比较 */
iot_ota_upgrade_info_t info = {0};
int rc = iot_ota_check_upgrade(iot, 0, desc->version, &info);
if (rc == OPRT_OK && info.has_upgrade) {
    ESP_LOGI(TAG, "upgrade -> %s  url=%s  size=%ld",
             info.version, info.url, info.file_size);
}
```

### 4. 上报状态、下载、烧写

```c
/* 下载前上报"升级中" */
iot_ota_report_status(iot, 0, OTA_STATUS_UPGRADING);

/* 用 esp_http_client 下载 info.url，逐块 esp_ota_write */
esp_err_t err = download_and_flash(info.url);
iot_ota_upgrade_info_free(iot, &info);

if (err != ESP_OK) {
    iot_ota_report_status(iot, 0, OTA_STATUS_UPGRD_EXEC);
    return;
}

/* 成功后上报"完成"，然后重启 */
iot_ota_report_status(iot, 0, OTA_STATUS_UPGRAD_FINI);
esp_restart();
```

`download_and_flash` 的核心流程（完整代码见 demo）：

```c
static esp_err_t download_and_flash(const char *url)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);

    esp_http_client_config_t http_cfg = {
        .url              = url,
        .timeout_ms       = 30000,
        .buffer_size      = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* 公共 CA 包 */
    };
    /* ... open / fetch headers / 检查 200 ... */

    esp_ota_handle_t handle;
    esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);

    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        esp_ota_write(handle, buf, n);   /* 逐块写入 */
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
- **栈要足够大**——TLS 握手 + HTTP 缓冲 + `esp_ota_write` 需要较大栈空间（demo 用 16KB）。
- **回报时机**——`UPGRADING` 在下载前、`FINI` 在重启前、`EXEC` 在失败时；漏报会导致云端升级面板状态不准。
- **MD5/HMAC 可选校验**——`info.md5` / `info.hmac` 可能为 NULL；若存在，建议在 `esp_ota_end` 后做一次校验再切分区。
