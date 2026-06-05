# Tuya Agentic-kit

智能硬件接入 Tuya AI 平台的多模态端侧 SDK，支持语音对话、图片理解/生成、设备侧 MCP。

## 特性

- 语音/文本对话，低延迟
- 图片理解与生成
- 设备侧 MCP（Model Context Protocol）支持
- 芯片和操作系统无关：macOS、Linux、FreeRTOS (ESP32)、MIPS、ARM
- 全球化部署，支持多数据中心区域

## SDK 模块

| 模块 | 头文件 | 说明 |
|------|--------|------|
| RTC TCP Client | `tuya_ai.h` | tRTC(tuya自研RTC协议) TCP 实现，开源实现，PAL 可移植 |
| RTC Client | `stm_open.h` | tRTC(tuya自研RTC协议) UDP 实现，预编译静态库形式 |
| IoT Client | `iot_client.h` | 设备激活、MQTT 连接、会话令牌获取 |
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
./build/tai_chat_demo

# 设备扫码配网
./build/scan_by_device_pair_demo

# App 扫码配网
./build/scan_by_app_pair_demo
```

## 项目结构

```
modules/
  rtc-tcp-client/     # tRTC(tuya自研RTC协议) TCP 实现（开源）
  rtc-client/         # tRTC(tuya自研RTC协议) UDP 实现（预编译库）
  iot-client/         # 设备激活、MQTT、token
  tuya-ble/           # BLE 配网模块
examples/
  posix/              # macOS/Linux 示例
  esp-idf/            # ESP32 示例
pal/                  # 平台抽象层
common/               # 公共工具代码
third_party/          # 第三方库
docs-site/            # 文档站点
```


