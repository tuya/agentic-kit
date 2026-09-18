---
title: Firmware OTA Upgrade
sidebar_label: OTA Upgrade
sidebar_position: 6
---

# Firmware OTA Upgrade

This guide explains how to use agentic-kit's `iot_ota` API to implement device firmware OTA (Over-The-Air) upgrades.

The SDK provides only **cloud protocol primitives**—version reporting, upgrade queries, status reporting, and an App confirmation notification callback. **The application owns firmware download, verification, flashing, partition management, and rollback protection** (for example, through ESP-IDF's `esp_ota_*` APIs or a vendor-specific bootloader API, with the SDK's streaming digest helper used for verification). See `examples/esp-idf/ota-demo` for a complete proactive-query example.

If the product requires an upgrade to proceed only after the user confirms it in the App, configure the cloud OTA task for App confirmation mode and register `ota_confirm_callback` on the device. agentic-kit does not call `tuya.device.upgrade.silent.get`, so it does not proactively retrieve or execute silent upgrade tasks.

## How It Works {#工作原理}

```
Device starts ──> iot_client_init (automatically reports current version)
                         │
                         v
                iot_ota_check_upgrade() ──> Cloud returns upgrade information
                         │                  (URL / version / size / hash)
                   Upgrade available?
                    /             \
                  No              Yes
                  │                │
             Keep running   iot_ota_report_status(UPGRADING)
                                   │
                                   v
                         Download firmware (info.url) + flash it  ← Application implements
                                   │
                             ┌─────┴─────┐
                           Success     Failure
                             │            │
                 report_status(FINI)  report_status(EXEC)
                             │            │
                       Restart       Retry / give up
```

## The Three APIs {#三个-api}

| API | ATOP interface | Purpose |
|-----|----------------|---------|
| `iot_ota_report_version` | `tuya.device.versions.update` (v4.1) | Reports the current firmware version (`iot_client_init` automatically calls it with `iot_client_config_t.sw_ver`, or the SDK default `IOT_SDK_SW_VER` when NULL); if the cloud already has the current version, set `skip_version_report = true` to skip automatic reporting during init |
| `iot_ota_check_upgrade` | `tuya.device.upgrade.get` (v4.4) | Checks whether firmware is waiting to be upgraded and returns its URL / version / size / hash (the cloud compares against the reported version, so the version number is no longer passed) |
| `iot_ota_report_status` | `tuya.device.upgrade.status.update` (v4.1) | Reports upgrade lifecycle status |
| `iot_ota_verify_init/update/finish` | — | Verifies the downloaded firmware's md5/hmac digest as a stream (see below) |

## Triggering an Upgrade After App Confirmation {#app-确认后触发升级}

After the App confirms the upgrade, the cloud notifies the device through MQTT protocol number `15`. After decrypting the message, the SDK reads `data.firmwareType` as the firmware channel and calls `ota_confirm_callback`:

```c
static volatile bool g_ota_confirmed;
static volatile int g_ota_channel;

static void on_ota_confirmed(int channel, void *user_data)
{
    (void)user_data;
    g_ota_confirmed = true;   /* Only signal briefly; do not block the MQTT process thread */
    g_ota_channel = channel;
}

iot_client_config_t cfg = {
    /* ... */
    .ota_confirm_callback = on_ota_confirmed,
    .ota_confirm_user_data = NULL,
};
```

After receiving this signal, the application's main loop or dedicated OTA worker thread runs the upgrade primitives:

```text
User confirms in App ──> Cloud sends MQTT protocol 15
                                │
                                v
                   ota_confirm_callback(channel)      /* SDK only notifies; it does not upgrade */
                                │
                      Application worker wakes
                                │
                                v
                   iot_ota_check_upgrade(client, channel, &info)
                                │
                        Upgrade available?
                          /             \
                        No              Yes
                        │                │
                   Keep running   report_status(UPGRADING)
                                         │
                                         v
                              Download + verify + flash  /* Application implements */
                                         │
                                  Success / failure
                                    │           │
                             report_status  report_status
                               (COMPLETE)      (ERROR)
```

```c
iot_ota_upgrade_info_t info = {0};
if (!g_ota_confirmed) {
    /* Wait for the confirmation signal */
}

int rc = iot_ota_check_upgrade(client, g_ota_channel, &info);
if (rc == OPRT_OK && info.has_upgrade) {
    rc = iot_ota_report_status(client, info.channel, OTA_STATUS_UPGRADING);
    /* Download, run iot_ota_verify_* verification, and perform platform OTA flashing
       on an application thread */
}
iot_ota_upgrade_info_free(client, &info);
```

Like `message_callback`, `ota_confirm_callback` runs on the thread that calls `iot_client_process()`. After the coreMQTT callback returns, that thread still needs to process acknowledgments and network buffers. Inside the callback, only set a flag, release a semaphore, or enqueue a work item. Do not call `iot_ota_check_upgrade()`, download firmware, write flash, or disconnect or destroy the IoT client. If this callback is not registered, protocol 15 continues to pass through to `message_callback`, preserving compatibility with older applications that parse it themselves.

### Upgrade Information Returned by `iot_ota_check_upgrade` {#iot_ota_check_upgrade-返回的升级信息}

```c
typedef struct {
    bool  has_upgrade;   // Whether the cloud has an upgrade
    char *version;       // Target version number
    char *url;           // Firmware download URL (prefer cdnUrl, fall back to httpsUrl)
    long  file_size;     // Firmware size in bytes
    int   channel;       // Firmware channel (0 = main MCU)
    char *md5;           // MD5 digest (may be NULL)
    char *hmac;          // HMAC digest (may be NULL)
} iot_ota_upgrade_info_t;
```

> These fields are heap-allocated and must be released with `iot_ota_upgrade_info_free()` after use.

### Upgrade Status Enum {#升级状态枚举}

```c
typedef enum {
    OTA_STATUS_IDLE      = 0,  // Default; no upgrade required
    OTA_STATUS_READY     = 1,  // Device is ready (upgrade task has been delivered)
    OTA_STATUS_UPGRADING = 2,  // Upgrade in progress (before download/flashing)
    OTA_STATUS_COMPLETE  = 3,  // Upgrade succeeded (report before restarting)
    OTA_STATUS_ERROR     = 4,  // Upgrade failed / error occurred
} iot_ota_status_t;
```

## Firmware Digest Verification (md5 / hmac) {#固件摘要校验md5--hmac}

The cloud returns a firmware digest in the upgrade information (`info.hmac` takes precedence, otherwise `info.md5`; a missing field or empty string both mean that digest was not delivered). The SDK provides a **streaming verification API**: the application feeds every firmware block to the verifier during the download loop, then `iot_ota_verify_finish()` compares the digest against the cloud-provided value after the download completes. A mismatch returns `OPRT_OTA_VERIFY_FAILED`.

The algorithm matches TuyaOpen:

```
expected = HMAC-SHA256(key = device secret_key,
                       msg = UPPERCASE_hex(SHA-256(firmware bytes)))
```

The HMAC message is the SHA-256 digest's **64-character uppercase hexadecimal string** (matching TuyaOpen's `hex2str`, not lowercase and not the raw 32 bytes). If the cloud does not deliver `hmac` (the field is missing or an empty string), verification falls back to comparing `MD5(firmware bytes)`. However, if `hmac` is non-empty but has an invalid length, `init` fails immediately and does not fall back to `md5`. Comparisons are case-insensitive.

```c
iot_ota_verify_ctx_t *ctx = NULL;
int rc = iot_ota_verify_init(iot, &info, &ctx);
if (rc != OPRT_OK && rc != OPRT_NOT_SUPPORTED) {
    /* Invalid digest format, out of memory, etc. — abort the upgrade and report OTA_STATUS_ERROR */
    return -1;
}
/* rc == OPRT_NOT_SUPPORTED: the cloud provided no digest field, ctx remains NULL,
 * so skip verification (the application decides whether to proceed) */

while ((n = read_firmware_chunk(buf)) > 0) {
    if (ctx != NULL && iot_ota_verify_update(ctx, buf, n) != OPRT_OK) {
        iot_ota_verify_abort(ctx);
        return -1;
    }
    flash_write(buf, n);                  /* Exactly the same data fed to the verifier */
}

if (ctx != NULL) {
    rc = iot_ota_verify_finish(ctx);      /* Frees ctx internally */
    if (rc != OPRT_OK) {
        /* OPRT_OTA_VERIFY_FAILED: firmware was tampered with/corrupted — abort and report OTA_STATUS_ERROR */
        return -1;
    }
}
```

- If verification fails, **do not switch the boot partition**. Report `OTA_STATUS_ERROR`, then discard this download.
- If the download fails partway through, call `iot_ota_verify_abort(ctx)` to release the context without comparing the digest.
- `finish` releases the context whether it succeeds or fails; do not use the context afterward. `finish(NULL)` does not mean "skip verification" and returns an invalid-parameter error, so a path that skips verification must guard both `update` and `finish` with `if (ctx != NULL)`.
- If `init` returns **anything other than `OPRT_OK` or `OPRT_NOT_SUPPORTED`**, the upgrade must be aborted. In that case, `ctx` is not written and remains NULL. Branch on these two values rather than enumerating individual error codes.

## Complete Example (ESP-IDF) {#完整示例esp-idf}

The following steps are taken from `examples/esp-idf/ota-demo/main/main.c`, which uses `esp_http_client` for downloads and `esp_ota_*` for flashing.

### 1. Partition Table {#1-分区表}

OTA requires two app partitions (`ota_0` / `ota_1`) and one `otadata` partition. The demo uses this `partitions.csv` (16 MB flash, with two 4 MB app partitions that can each hold firmware of approximately 4 MB):

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
otadata,  data, ota,     0x10000, 0x2000,
ota_0,    app,  ota_0,   0x20000, 4M,
ota_1,    app,  ota_1,   ,        4M,
```

### 2. Key sdkconfig Options {#2-sdkconfig-关键项}

```ini
# Reserve enough stack for TLS + HTTP + esp_ota
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
# 16 MB flash (holds two 4 MB OTA partitions)
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
# Custom partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
# Enable the public CA certificate bundle (required for cdnUrl downloads)
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

### 3. Initialization and Upgrade Query {#3-初始化与升级查询}

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
    /* Disable automatic connection: use only ATOP HTTP, without MQTT */
    .mqtt_disable_auto_connect = true,
    /* Application firmware version: automatically reported during init for cloud OTA
       comparison (NULL uses the SDK default) */
    .sw_ver     = desc->version,
    /* Public CA certificate bundle: required by ATOP HTTPS
       (version reporting/upgrade queries/status reporting) */
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};

/* iot_init(pal) must be called before iot_client_init; otherwise iot_client_init returns NULL */
iot_init(tai_pal_freertos());

iot_client_t *iot = iot_client_init(&iot_cfg);

/* Check for an upgrade (the cloud compares against sw_ver reported during init;
   there is no need to pass the version number again) */
iot_ota_upgrade_info_t info = {0};
int rc = iot_ota_check_upgrade(iot, 0, &info);
if (rc == OPRT_OK && info.has_upgrade) {
    ESP_LOGI(TAG, "upgrade -> %s  url=%s  size=%ld",
             info.version, info.url, info.file_size);
}
```

### 4. Report Status, Download, and Flash {#4-上报状态下载烧写}

```c
/* Report "upgrading" before the download */
iot_ota_report_status(iot, 0, OTA_STATUS_UPGRADING);

/* Use esp_http_client to download info.url, calling esp_ota_write and digest
   verification for each block */
esp_err_t err = download_and_flash(iot, &info);
iot_ota_upgrade_info_free(iot, &info);

if (err != ESP_OK) {
    iot_ota_report_status(iot, 0, OTA_STATUS_ERROR);
    return;
}

/* Report "complete" after success, then restart */
iot_ota_report_status(iot, 0, OTA_STATUS_COMPLETE);
esp_restart();
```

Core flow of `download_and_flash` (see the demo for the complete code):

```c
static esp_err_t download_and_flash(iot_client_t *iot,
                                    const iot_ota_upgrade_info_t *info)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);

    /* Digest verification context (returns OPRT_NOT_SUPPORTED and leaves verify NULL
       when the cloud provides neither md5 nor hmac) */
    iot_ota_verify_ctx_t *verify = NULL;
    int vrc = iot_ota_verify_init(iot, info, &verify);
    if (vrc != OPRT_OK && vrc != OPRT_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "iot_ota_verify_init failed: %d", vrc);
        return ESP_FAIL;   /* The verifier could not be created — do not install this firmware */
    }

    esp_http_client_config_t http_cfg = {
        .url              = info->url,
        .timeout_ms       = 30000,
        .buffer_size      = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* Public CA bundle */
    };
    /* ... open / fetch headers / check for 200; every failure path must call
       iot_ota_verify_abort(verify) ... */

    esp_ota_handle_t handle;
    esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);

    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        if (verify != NULL) {
            iot_ota_verify_update(verify, (const uint8_t *)buf, n);  /* Same data being written */
        }
        esp_ota_write(handle, buf, n);                               /* Write block by block */
    }

    /* Verify the cloud-provided digest before switching partitions;
       call esp_ota_abort to discard the image on a mismatch */
    if (verify != NULL) {
        vrc = iot_ota_verify_finish(verify);   /* Frees verify internally */
        if (vrc != OPRT_OK) {
            ESP_LOGE(TAG, "Firmware digest mismatch (rc=%d)", vrc);
            esp_ota_abort(handle);
            return ESP_FAIL;
        }
    }

    esp_ota_end(handle);
    esp_ota_set_boot_partition(part);    /* Switch the boot partition */
    return ESP_OK;
}
```

### 5. First-Boot Validation (Rollback Protection) {#5-首次启动验证防回滚}

After restarting, the new firmware should mark itself valid; otherwise, ESP-IDF rolls back to the old partition after several restarts:

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

Call this once at the start of `app_main`.

## Build and Flash {#构建与烧写}

```bash
cd examples/esp-idf/ota-demo
idf set-target esp32s3
idf build
idf flash monitor
```

The first flash writes to `ota_0`; later OTA upgrades write to `ota_1` and switch the boot partition.

## Important Notes {#注意事项}

- **The SDK does not download or flash firmware**—`iot_ota` handles only the cloud protocol. The application implements download verification, partition management, and rollback protection.
- **App confirmation mode**—the cloud task must be configured for App confirmation mode. The device receives protocol 15 through `ota_confirm_callback`, after which an application worker queries and performs the upgrade. The SDK does not call the silent-upgrade interface.
- **Provide enough stack space**—the TLS handshake, HTTP buffers, and `esp_ota_write` require a relatively large stack (the demo uses 16 KB).
- **Report at the right time**—report `UPGRADING` before downloading, `COMPLETE` before restarting, and `ERROR` on failure. Missing reports make the cloud upgrade panel inaccurate.
- **MD5/HMAC digest verification**—`iot_ota_verify_init/update/finish` computes the digest as a stream during download. Complete verification **before** `esp_ota_set_boot_partition`; a mismatch must abort the upgrade (see "Firmware Digest Verification" above).
