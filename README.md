# Tuya Agentic-kit

智能硬件接入 Tuya AI 平台的多模态端侧 SDK，支持语音对话、图片理解/生成、设备侧 MCP。

## 特性

- 语音/文本对话，低延迟
- 图片理解与生成
- 设备侧 MCP（Model Context Protocol）支持
- 设备数据点（DP）管理：schema 校验、本地缓存、批量上报、下行回调、状态持久化
- OTA 固件升级：云端协议（版本上报 / 升级检查 / 状态回报），下载与烧录由应用负责
- 芯片和操作系统无关：macOS、Linux、FreeRTOS (ESP32)、MIPS、ARM
- 全球化部署，支持多数据中心区域

## SDK 模块

| 模块 | 头文件 | 说明 |
|------|--------|------|
| RTC TCP Client | `tuya_ai.h` | tRTC(tuya自研RTC协议) TCP 实现，开源实现，PAL 可移植 |
| RTC Client | `stm_open.h` | tRTC(tuya自研RTC协议) UDP 实现，预编译静态库形式 |
| IoT Client | `iot_client.h`、`iot_dp.h`、`iot_ota.h` | 设备激活、MQTT 连接、会话令牌获取；数据点（DP）管理（schema 校验 / 缓存 / 上下行 / 持久化）；OTA 固件升级（云端协议） |
| Tuya BLE | `tuya_ble_nimble.h` | BLE 蓝牙配网（ESP-IDF） |

## 环境要求

| 工具 | 版本 | macOS | Linux (Debian/Ubuntu) |
|------|------|-------|----------------------|
| CMake | >= 3.20 | `brew install cmake` | `apt install cmake` |

Bundled 依赖（mbedTLS、cJSON、coreHTTP、coreMQTT）会自动编译，无需单独安装。

## 编译

```sh

git clone https://github.com/tuya/agentic-kit.git
cd agentic-kit

git submodule update --init --recursive

mkdir -p build && cd build
cmake .. && make
```

## 运行示例

编译 POSIX 平台示例：

```sh
cmake -S examples/posix -B build
cmake --build build
```

运行：

```sh
# 语音/文本聊天（RTC TCP Client）
./build/tai_text_chat_demo

# 设备扫码配网
./build/scan_by_device_pair_demo

# App 扫码配网
./build/scan_by_app_pair_demo

# 设备数据点（DP）管理
./build/dp_management_demo

# OTA 固件升级（云端协议）
./build/ota_demo
```

## 项目结构

```
modules/
  rtc-tcp-client/     # tRTC(tuya自研RTC协议) TCP 实现（开源）
  rtc-client/         # tRTC(tuya自研RTC协议) UDP 实现（预编译库）
  iot-client/         # 设备激活、MQTT、token、数据点（DP）管理
  tuya-ble/           # BLE 配网模块
examples/
  posix/              # macOS/Linux 示例
  esp-idf/            # ESP32 示例
pal/                  # 平台抽象层
common/               # 公共工具代码
third_party/          # 第三方库
docs-site/            # 文档站点
```

## 许可证

本项目整体基于 [Apache License 2.0](LICENSE) 开源。

**例外**：`modules/rtc-client/` 模块**不开源**，其中的预编译静态库（`libstm.a`）及配套头文件为 Tuya Inc. 专有资产。客户可自由使用、复制、分发该模块，包括嵌入到硬件产品中（无论是否结合 agentic-kit 使用），唯一限制是**不得对预编译库进行逆向工程、反编译或反汇编**。详见 [modules/rtc-client/LICENSE](modules/rtc-client/LICENSE)。


