---
title: 快速开始
sidebar_label: 快速开始
sidebar_position: 1
---

# 快速开始

> 💡 **没有硬件？** 本页全部示例均可在 macOS 或 Linux 上运行，无需开发板。

:::note 首次接入？
如果你是第一次使用涂鸦平台，请先阅读[介绍](../intro)和[核心概念](../concepts)，
了解产品 PID、设备授权码、配网等基础概念。
:::

:::tip 需要用自己的设备？
先完成[配网](./pair-overall)获取凭据，或[领取免费授权码](../get-authkey)。
:::

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

#### POSIX 系统示例

Posix 示例位于 `examples/posix/` 目录下，使用 CMake 构建系统：

```sh
cd examples/posix
mkdir -p build && cd build
cmake .. && make
```

> 构建时会通过 FetchContent 自动拉取示例所需的第三方库（qrcodegen、quirc、stb），无需手动安装。


#### ESP-IDF 系统示例

ESP-IDF 示例位于 `examples/esp-idf/` 目录下，使用 ESP-IDF 构建系统：

```sh
cd examples/esp-idf/ai/rtc-tcp-client
idf.py build
idf.py flash monitor
```

## 运行示例

编译成功后，在 `examples/posix/` 目录下运行（POSIX 平台示例）：

```sh
# --- AI 实时交互：rtc-tcp-client（源码）---
./build/tai_text_chat_demo                 # 文本对话
./build/tai_audio_chat_demo input.wav      # 语音对话（需 libopus；省略文件则发文字问候，WAV 须为单声道 16-bit）
./build/tai_edu_camera_demo res/test.jpg   # 拍照识物 + TTS
./build/tai_mcp_demo                       # 设备 MCP（initialize 握手 + 工具调用）

# --- AI 实时交互：rtc-client（预编译库，stm_open API）---
./build/chat_demo                          # 语音聊天

# 设备扫码配网示例（默认使用 res/qr.jpg）
./build/scan_by_device_pair_demo
```

> 上述 AI demo 均内置默认设备凭据，可直接运行；要用自己的设备时，多数 demo 支持追加 `[devid] [secret_key] [local_key]` 参数。`tai_audio_chat_demo` 仅在检测到 libopus 时才会编译。


