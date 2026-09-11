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

:::warning 必须连接 MQTT，App 才判定配网成功
**App 只有在检测到设备连接上涂鸦云 MQTT 通道（设备上线）后，才会判定配网成功。**
仅完成 Token 激活、拿到 `devid` 等凭据但不连接 MQTT，App 端会显示配网失败/超时。

因此这一点现在由默认行为保证——自动连接是默认开启的，无需额外配置；只要不设 `.mqtt_disable_auto_connect`，设备就会在激活完成后自动连接 MQTT（示例 `main/main.c` 中
已如此配置）；若保持 `false`，则必须在激活成功后立即手动调用
`iot_client_connect()`。
:::

## 关键代码

### 配置与启动

```c
#include "tuya_ble_nimble.h"
#include "app_config.h"

static void on_tuya_ble_prov_complete(const tuya_ble_wifi_creds_t *creds)
{
    // 收到来自 App 的 WiFi 凭据
    // creds 仅在回调期间有效；复制到应用持有的缓冲区（示例把日志开到 DEBUG，
    // SDK 的 [ble] 协议日志会打印含密码/Token 的凭据 JSON 原文，便于真机联调）
    s_wifi_creds = *creds;
    // 通知主线程继续
    xEventGroupSetBits(s_prov_event_group, PROV_DONE_BIT);
}

void app_main(void)
{
    // 初始化 NVS（NimBLE 需要）
    nvs_flash_init();
    iot_init_default(); // 在 BLE JSON 解析前初始化 PAL/cJSON 分配器

    tuya_ble_prov_cfg_t prov_cfg = {
        .device_name = "TYBLE",         // BLE 广播名称（最长 5 字符，超出会被截断）
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
#define TUYA_BLE_DEVICE_NAME  "TYBLE"   // 最长 5 字符（TUYA_BLE_NAME_MAX_LEN），超出会被截断
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
| `device_name` | `const char *` | BLE 广播设备名（最长 5 字符 `TUYA_BLE_NAME_MAX_LEN`，超出部分被静默截断） |
| `product_key` | `const char *` | 产品 PID |
| `uuid` | `const char *` | 设备 UUID |
| `auth_key` | `const char *` | 设备 Auth Key |
| `cb` | callback | 配网完成回调 |

### `tuya_ble_nimble_stop`

```c
int tuya_ble_nimble_stop(void);
```

停止 BLE 广播和服务，等待已接收的 WiFi 扫描任务结束，再释放队列和 NimBLE 资源。
必须由应用任务串行调用 start/stop，不能在 BLE 回调或 ESP 事件循环中调用 stop。
扫描采用逻辑取消，若驱动迟迟不返回，stop 也会等待。

**返回值：** `0` 表示已停止（重复停止也返回 `0`）；非零表示 `nimble_port_stop()` 失败，
资源保留以便重试；`nimble_port_deinit()` 失败（干净停止后几乎不会发生）会经
`ESP_ERROR_CHECK` 中止程序。
成功停止后，示例先停止扫描阶段启动的 WiFi，再配置凭据并重新启动 STA，
不依赖对已启动 STA 再次调用 start 产生连接事件。

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
- 切勿在激活配置中设 `.mqtt_disable_auto_connect = true`：**App 以设备 MQTT 上线作为配网成功的判定条件**，不连接 MQTT 时 App 会显示配网失败/超时。
- 需确保项目正确引用了 `modules/tuya-ble/` 和 `modules/iot-client/` 组件。

## BLE 核心移植约定

配置字符串必须在核心状态生命周期内有效，并以 NUL 结尾：`product_key` 为
16 字节、`auth_key` 为 32 字节，`uuid` 为 16 字节或 20 个字母/数字。
升级后重新编译调用方，公开状态结构的布局发生了变化。

在同一 BLE 执行上下文调用 `tuya_ble_prov_on_data()`、`tuya_ble_prov_tx_ready()`、
`tuya_ble_prov_tick()` 和 `tuya_ble_prov_close()`。连接关闭、重新连接或协议栈重置时，
调用 `close()` 清除会话密钥及排队数据；旧的 `reset_conn()` 仅重置传输状态。
`set_paired(true)` 不授予凭据下发权限。

通知默认最多 20 字节；MTU 协商后通过 `tuya_ble_prov_set_gatt_payload(state, mtu - 3)`
更新实际预算。发送回调必须在返回前复制数据：返回 0 表示接受，
`TUYA_BLE_SEND_BUSY` 表示未接受、稍后重试，其他值表示永久失败。
端口定期传入单调毫秒时间；未完成传输在 10 秒后过期。NimBLE 示例在其事件队列上
驱动重试和超时，不新增 MQTT 线程。凭据回调等待回复被传输层接受后才执行；
等待期间忽略新的凭据请求，不替换待交付凭据，也不为新请求发送 ACK。
Trsmitr 的乱序及超时诊断通过 SDK 日志 facade 输出。

`tuya_ble_prov_cfg_ext_t.random_fn` 可选，用于提供能报告已填充字节数的 CSPRNG；
短输出会使握手失败。未提供时，`tuya_ble_hal_random()` 必须完整填充缓冲区。

配置扫描 provider 后会启用 WiFi 列表及状态查询能力声明；NimBLE 示例已配置 provider。
端口工作任务执行 WiFi 扫描，通过队列复制原始 scan token 和结果，由 BLE 执行上下文交付给 SDK。
取消后不立即中断无线扫描，旧任务结束并被处理前不接收新扫描，避免旧结果冒用新 token。
状态查询仅返回固定 CFG 状态，没有主动阶段报告或完整 PSK3.0 activation exchange。
凭据仍通过传统 Token 配网流程接收；完成 BLE 凭据接收不表示已经完成云端激活。
