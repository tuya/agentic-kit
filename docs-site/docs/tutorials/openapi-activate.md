---
title: OpenAPI 配网
sidebar_label: OpenAPI 配网
sidebar_position: 6
---

# OpenAPI 配网

> 对应示例：`examples/posix/pair/api-activate/`

本章介绍第三种配网方式：**不依赖涂鸦 App，通过涂鸦 OpenAPI（Cloud API）在
服务端完成用户创建和配网 Token 生成，再将 Token 传给设备完成激活**。

前两种配网方式都需要用户安装并使用涂鸦 App 来完成扫码配网。但在某些场景下，
设备厂商可能：

- 拥有自己的 App 或后台系统，不希望依赖涂鸦 App
- 需要在生产线上批量激活设备
- 设备没有屏幕也没有摄像头

此时可以通过涂鸦 OpenAPI 直接在服务端完成用户同步和配网 Token 的生成，然后
通过任意方式（串口、蓝牙、HTTP 等）将 Token 传递给设备，设备调用
`iot_client_init_on_boarding_with_token()` 即可完成激活。

## 整体流程

```
[服务端 / 脚本]                              [设备端]
      |                                          |
      |  1. POST /v1.0/apps/{schema}/user        |
      |     (创建/同步用户, 获得 uid)             |
      |                                          |
      |  2. POST /v1.0/device/paring/token       |
      |     (生成配网 token, region, secret)      |
      |                                          |
      |  3. 拼接: {region}{token}{secret}         |
      |     传递给设备 ----------------------->   |
      |                                          |
      |                          4. iot_client_init_on_boarding_with_token()
      |                             (用 token 激活设备)
      |                                          |
      |  5. GET /v1.0/device/paring/tokens/{token}|
      |     (轮询确认配网结果)                    |
```

## 涉及的 OpenAPI

### 1. 用户同步 — `POST /v1.0/apps/{schema}/user`

在涂鸦云中创建或同步一个用户，返回用户 `uid`。

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `country_code` | string | 是 | 国家码，如 `"86"` |
| `username` | string | 是 | 用户名 |
| `password` | string | 是 | 密码（MD5 哈希值） |
| `username_type` | int | 是 | 1=手机号, 2=邮箱, 3=其他 |

### 2. 生成配网 Token — `POST /v1.0/device/paring/token`

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `uid` | string | 是 | 用户 uid |
| `paring_type` | string | 是 | 配网类型：`BLE`、`AP`、`EZ` |
| `time_zone_id` | string | 是 | 时区 |

### 3. 查询配网结果 — `GET /v1.0/device/paring/tokens/{token}`

查询设备是否已通过该 Token 完成激活。

## Token 格式

设备端接收的 Token 需要由三部分拼接而成：

```
{region}{token}{secret}
```

例如：`region="AY"`, `token="H73H8u7A"`, `secret="p4pX"` → 完整 Token 为 `AYH73H8u7Ap4pX`。

设备端的 `iot_client_init_on_boarding_with_token()` 会自动从前两个字符解析 Region 信息。

## 环境配置

| 环境变量 | 说明 | 示例 |
|----------|------|------|
| `TUYA_CLIENT_ID` | 涂鸦 IoT 平台的 Access ID | `xwm........` |
| `TUYA_CLIENT_SECRET` | 涂鸦 IoT 平台的 Access Secret | `95f13d4a616d407a...` |
| `TUYA_BASE_URL` | OpenAPI 地址 | `https://openapi.tuyacn.com` |
| `SCHEMA` | App schema 标识（shell 脚本使用；Python 脚本也支持 `TUYA_SCHEMA` 或 `--schema` 参数） | `marsid` |

不同数据中心对应的 `TUYA_BASE_URL`：

| 数据中心 | URL |
|----------|-----|
| 中国 | `https://openapi.tuyacn.com` |
| 美西 | `https://openapi.tuyaus.com` |
| 欧洲 | `https://openapi.tuyaeu.com` |
| 印度 | `https://openapi.tuyain.com` |

## 运行示例

```sh
export TUYA_CLIENT_ID="your_client_id"
export TUYA_CLIENT_SECRET="your_secret"
export TUYA_BASE_URL="https://openapi.tuyacn.com"
export SCHEMA="your_schema"

# 一键脚本
./build/activate-demo.sh

# 或分步执行
python3 ./build/tuya_openapi.py sync-user --schema marsid --country-code 86 \
    --username "test_user" --password "mypassword" --username-type 3

python3 ./build/tuya_openapi.py pairing-token --uid "ay..." --paring-type BLE \
    --time-zone-id "Asia/Shanghai"

./build/activate_demo "$PAIRING_TOKEN" <uuid> <authkey> <product_key>
```

## 注意事项

- 配网 Token 有有效期（通常 100 秒），需在有效期内完成设备激活。
- `tuya_openapi.py` 使用 Python 标准库实现，无需安装额外依赖。
- 实际产品中，OpenAPI 调用应在厂商自己的后台服务中完成，**不应将 Access
  Secret 暴露在客户端或设备端**。
