# OTA Demo (ESP-IDF + agentic-kit)

Demonstrates a complete firmware OTA upgrade flow using the agentic-kit `iot_ota` API for cloud communication and ESP-IDF `esp_ota_*` + `esp_http_client` for the actual firmware download and flash.

## Flow

1. Connect to WiFi
2. Initialize `iot_client` with device credentials
3. Call `iot_ota_check_upgrade()` — queries the Tuya cloud for available firmware
4. If upgrade available, call `iot_ota_report_status(UPGRADING)`
5. Download firmware via `esp_http_client` and flash via `esp_ota_write()`
6. Call `iot_ota_report_status(SUCCESS)` (or `FAILED` on error)
7. Reboot into the new firmware

## Setup

1. Edit `main/app_config.h`:
   - `WIFI_SSID` / `WIFI_PASSWORD` — your WiFi credentials
   - `DEFAULT_DEVID` / `DEFAULT_SECRET_KEY` / `DEFAULT_LOCAL_KEY` — device credentials from activation

2. Build and flash:
   ```sh
   idf set-target esp32s3
   idf build
   idf -p /dev/cu.usbmodem* flash monitor
   ```

## Partition Table

Uses a custom partition table (`partitions.csv`) with two 4MB OTA partitions (`ota_0`, `ota_1`) on 16MB flash, sized for firmware images up to ~4MB. The demo binary itself is ~1.1MB (WiFi + TLS + HTTP + OTA).

## How It Works

The SDK (`iot_ota.h`) provides three cloud-protocol primitives:
- `iot_ota_report_version()` — report current firmware version
- `iot_ota_check_upgrade()` — query cloud for new firmware (returns URL, version, size, md5, hmac)
- `iot_ota_report_status()` — report upgrade lifecycle status

The application implements the download + flash logic using ESP-IDF APIs:
- `esp_http_client` — streaming HTTP download of the firmware image
- `esp_ota_begin/write/end` — write firmware to the inactive OTA partition
- `esp_ota_set_boot_partition` — switch boot to the new partition
- `esp_restart` — reboot into the new firmware

On first boot after an OTA, `esp_ota_mark_app_valid_cancel_rollback()` is called to confirm the new firmware is healthy (anti-rollback protection).
