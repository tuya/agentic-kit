---
title: 快速开始
sidebar_label: 快速开始
sidebar_position: 1
---

# 快速开始

## 依赖

| 工具/库 | 版本 | macOS 安装 | Linux (Debian/Ubuntu) 安装 |
|---------|------|-----------|---------------------------|
| CMake | ≥ 3.20 | `brew install cmake` | `apt install cmake` |

> 构建系统会自动编译 bundled 的 mbedTLS、cJSON、coreHTTP、coreMQTT 依赖，无需单独安装。

## 编译

### agentic-kit 代码编译

```sh
git clone https://github.com/tuya/agentic-kit.git
cd agentic-kit

git submodule update --init --recursive

mkdir -p build && cd build
cmake .. && make
```

CMakeLists.txt 会自动选择平台对应的预编译库目录：

| 平台 | 库目录 |
|------|--------|
| macOS arm64 | `libs/macos_arm64/` |
| Linux x86_64 | `libs/linux-gnu-amd64/` |
| Linux aarch64 | `libs/linux-gnu-aarch64/` |

其他可用预编译库（需手动指定）：`libs/rockchip830-arm/`、`libs/ingenic-mips/`。


### 示例代码编译

#### Posix系统示例

Posix 示例位于 `examples/posix/` 目录下，使用 CMake 构建系统：

```sh
cd examples/posix
mkdir -p build && cd build
cmake .. && make
```

> 构建时会通过 FetchContent 自动拉取示例所需的第三方库（qrcodegen、quirc、stb），无需手动安装。


#### ESP-IDF系例示例

ESP-IDF 示例位于 `examples/esp-idf/` 目录下，使用 ESP-IDF 构建系统：

```sh
cd examples/esp-idf/ai/rtc-tcp-client
idf.py build
idf.py flash monitor
```

## 运行示例

编译成功后，在 `examples/posix/` 目录下运行（POSIX 平台示例）：

```sh
# 语音聊天示例
./build/chat_demo

# 设备扫码配网示例（默认使用 res/qr.jpg）
./build/scan_by_device_pair_demo
```

## 项目结构

```
├── modules/                # SDK 模块源码/库
│   ├── rtc-tcp-client/     # tRTC(tuya自研RTC协议) TCP 实现（源码，推荐）
│   │   ├── include/tuya_ai.h
│   │   └── src/
│   ├── rtc-client/         # tRTC(tuya自研RTC协议) UDP 实现（预编译库）
│   │   ├── include/stm_open.h
│   │   └── libs/
│   ├── iot-client/         # IoT 客户端（激活、MQTT、token）
│   │   ├── include/iot_client.h
│   │   └── libs/
│   └── tuya-ble/           # BLE 配网模块
├── examples/
│   ├── posix/              # macOS/Linux 示例
│   │   ├── ai/
│   │   │   ├── rtc-tcp-client/    # 使用 rtc-tcp-client 的语音聊天
│   │   │   └── rtc-client/        # 使用 rtc-client 的语音聊天
│   │   ├── pair/
│   │   │   ├── scan-by-device/    # 设备扫码配网
│   │   │   ├── scan-by-app/       # App 扫码配网
│   │   │   └── api-activate/      # OpenAPI 激活
│   │   └── res/                   # 测试资源文件
│   └── esp-idf/            # ESP-IDF 示例
│       ├── ai/
│       │   └── rtc-tcp-client/    # ESP32 语音聊天
│       └── pair/
│           ├── pair-by-ble/       # BLE 蓝牙配网
│           └── scan-by-app/       # App 扫码配网
├── pal/                    # Platform Abstraction Layer
├── common/                 # 公共工具代码
├── third_party/            # 第三方库（mbedtls 等）
└── docs-site/              # 本文档站点
```
