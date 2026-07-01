---
title: BLE 蓝牙配网
sidebar_label: BLE 蓝牙配网
sidebar_position: 7
---

# BLE 蓝牙配网

> 对应示例：`examples/esp-idf/pair/pair-by-ble/`

本章介绍如何在嵌入式设备上通过 BLE（蓝牙低功耗）实现设备配网。涂鸦 App 通过 BLE 连接将 Wi-Fi 凭据和配网 Token 传递给设备，设备无需摄像头或屏幕即可完成配网。

## 适用场景

- 设备支持蓝牙功能（支持 BLE 4.2+，NimBLE 协议栈）
- 设备没有摄像头或屏幕，但需要通过涂鸦 App 配网
- 设备尚未连接 Wi-Fi（BLE 配网的核心作用就是传递 Wi-Fi 凭据）

:::note 平台说明
当前示例基于 ESP-IDF + NimBLE 协议栈，其他平台需自行适配 BLE 层。
:::

## 整体流程

```
nvs_flash_init()                    // 1. 初始化 NVS（BLE 需要）
        |
        v
tuya_ble_nimble_start(&prov_cfg)    // 2. 启动 BLE 广播，等待 App 连接
        |
        v
（App 通过 BLE 发送 WiFi SSID/密码/Token）
        |
        v
on_tuya_ble_prov_complete(creds)    // 3. 回调：收到 WiFi 凭据和 Token
        |
        v
tuya_ble_nimble_stop()              // 4. 停止 BLE 广播
        |
        v
（使用 WiFi 凭据连接网络）           // 5. 连接 WiFi
        |
        v
iot_client_init_on_boarding_with_token(token)  // 6. 用 Token 激活设备
```

## 关键代码

### 配置与启动

```c
#include "tuya_ble_nimble.h"
#include "app_config.h"

static void on_tuya_ble_prov_complete(const tuya_ble_wifi_creds_t *creds)
{
    // 收到来自 App 的 WiFi 凭据
    printf("ssid=%s\n", creds->ssid);
    printf("password=%s\n", creds->password);
    printf("token=%s\n", creds->token);
    // 通知主线程继续
    xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);
}

void app_main(void)
{
    // 初始化 NVS（NimBLE 需要）
    nvs_flash_init();

    tuya_ble_prov_cfg_t prov_cfg = {
        .device_name = "TuyaDevice",    // BLE 广播名称
        .product_key = PRODUCT_KEY,     // 产品 PID
        .uuid        = DEVICE_UUID,     // 设备 UUID
        .auth_key    = AUTH_KEY,        // 设备 Auth Key
        .cb          = on_tuya_ble_prov_complete,
    };

    tuya_ble_nimble_start(&prov_cfg);

    // 等待配网完成
    xEventGroupWaitBits(s_prov_event_group, PROV_DONE_BIT,
                        pdTRUE, pdFALSE, portMAX_DELAY);

    tuya_ble_nimble_stop();

    // 接下来：用 creds->ssid/password 连接 WiFi
    // 然后用 creds->token 调用 iot_client_init_on_boarding_with_token()
}
```

### 配置文件 `app_config.h`

```c
#define TUYA_BLE_DEVICE_NAME  "TuyaDevice"
#define PRODUCT_KEY           "your_product_key"
#define DEVICE_UUID           "your_uuid"
#define AUTH_KEY              "your_auth_key"
```

> 说明：当前示例直接从 `main/app_config.h` 读取这些配置项，请以仓库中的实际示例文件为准。

## API 参考

### `tuya_ble_nimble_start`

```c
int tuya_ble_nimble_start(const tuya_ble_prov_cfg_t *cfg);
```

启动 BLE 广播和 GATT 服务，等待涂鸦 App 连接并传递配网信息。

**返回值：** `0` 成功，非零表示错误。

**`tuya_ble_prov_cfg_t` 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `device_name` | `const char *` | BLE 广播设备名 |
| `product_key` | `const char *` | 产品 PID |
| `uuid` | `const char *` | 设备 UUID |
| `auth_key` | `const char *` | 设备 Auth Key |
| `cb` | callback | 配网完成回调 |

### `tuya_ble_nimble_stop`

```c
int tuya_ble_nimble_stop(void);
```

停止 BLE 广播和服务，释放 NimBLE 资源。

**返回值：** `0` 成功，非零表示错误。

### `tuya_ble_wifi_creds_t`

回调中收到的结构体：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ssid` | `char[]` | WiFi SSID |
| `password` | `char[]` | WiFi 密码 |
| `token` | `char[]` | 配网 Token（用于后续设备激活） |

## 编译与运行

```sh
cd examples/esp-idf/pair/pair-by-ble
idf.py set-target esp32s3   # 根据你的芯片选择
idf.py build
idf.py flash monitor
```

## sdkconfig 要点

`sdkconfig.defaults` 中当前包含的关键配置：

- `CONFIG_BT_ENABLED=y` — 启用蓝牙
- `CONFIG_BT_NIMBLE_ENABLED=y` — 使用 NimBLE 协议栈
- `CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y` — 仅启用 BLE 控制器模式

## 注意事项

- BLE 配网完成后应尽快停止 BLE 广播（`tuya_ble_nimble_stop`），避免与 WiFi 共存时的射频冲突。
- Token 格式与其他配网方式一致：前两字符为 Region 编码。
- 配网完成后的设备激活流程与[设备扫码配网](./scan-by-device)相同，使用 `iot_client_init_on_boarding_with_token()`。
- 需确保项目正确引用了 `modules/tuya-ble/` 和 `modules/iot-client/` 组件。
