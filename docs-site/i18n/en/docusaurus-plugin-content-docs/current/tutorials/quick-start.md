---
title: Quick start
sidebar_label: Quick start
sidebar_position: 1
---

# Quick start

> 💡 **No hardware?** Every example on this page runs on macOS or Linux, with no development board required.

:::note First time integrating?
If you are using the Tuya platform for the first time, read [Introduction](../intro) and [Core concepts](../concepts)
first to understand the basics such as Product PID, device authorization code, and Provisioning.
:::

:::tip Need to use your own device?
Complete [Provisioning](./pair-overall) first to obtain credentials, or [claim a free authorization code](../get-authkey).
:::

## Dependencies {#依赖}

| Tool / library | Version | macOS install | Linux (Debian/Ubuntu) install |
|---------|------|-----------|---------------------------|
| CMake | ≥ 3.20 | `brew install cmake` | `apt install cmake` |
| Python3 | ≥ 3.x | `brew install python3` | `apt install python3` |

> The build system automatically compiles the bundled mbedTLS, cJSON, coreHTTP, and coreMQTT dependencies; no separate installation is required.

## Build {#编译}

### Building agentic-kit {#agentic-kit-代码编译}

```sh
git clone https://github.com/tuya/agentic-kit.git
cd agentic-kit

git submodule update --init --recursive

mkdir -p build && cd build
cmake .. && make
```

CMakeLists.txt automatically selects the precompiled library directory for the platform (located under `modules/rtc-client/libs/`):

| Platform | Library directory |
|------|--------|
| macOS arm64 | `modules/rtc-client/libs/macos_arm64/` |
| Linux x86_64 | `modules/rtc-client/libs/linux-gnu-amd64/` |
| Linux aarch64 | `modules/rtc-client/libs/linux-gnu-aarch64/` |

Other available precompiled libraries: `modules/rtc-client/libs/rockchip830-arm/` and `modules/rtc-client/libs/ingenic-mips/`. When cross-compiling for these two platforms, the library-directory selection logic is hard-coded in the platform checks in the root `CMakeLists.txt` (`STEAM_CLIENT_LIB_DIR`); you must modify that location to point to the corresponding directory.


### Building the example code {#示例代码编译}

#### POSIX examples {#posix-系统示例}

The POSIX examples are located under the `examples/posix/` directory and use the CMake build system:

```sh
cd examples/posix
mkdir -p build && cd build
cmake .. && make
```

> During the build, FetchContent automatically pulls the third-party libraries required by the examples (qrcodegen, quirc, stb); no manual installation is required.


#### ESP-IDF examples {#esp-idf-系统示例}

The ESP-IDF examples are located under the `examples/esp-idf/` directory and use the ESP-IDF build system:

```sh
cd examples/esp-idf/ai/rtc-tcp-client
idf.py build
idf.py flash monitor
```

## Running the examples {#运行示例}

After a successful build, run the following from the `examples/posix/` directory (POSIX platform examples):

```sh
# --- AI real-time interaction: rtc-tcp-client (source) ---
./build/text_chat_demo                 # Text chat
./build/audio_chat_demo input.wav      # Voice chat (requires libopus; if the file is omitted, a text greeting is sent; the WAV must be mono 16-bit)
./build/edu_camera_demo res/test.jpg   # Photo recognition + TTS
./build/music_play_demo                # Music playback (text triggers the music skill, downloads a preview clip)
./build/mcp_demo                       # Device MCP (initialize handshake + tool calls)
./build/agent_trigger_demo             # Agent trigger (reports a DP to trigger a cloud rule, receives proactive pushes)

# --- AI real-time interaction: rtc-client (precompiled library, stm_open API) ---
./build/udp_chat_demo                          # Voice chat

# Device QR code Provisioning example (uses res/qr.jpg by default)
./build/scan_by_device_pair_demo

# Cloud device removal / factory reset notification (passively receives protocol 11)
./build/unbind_demo <devid> <secret_key> <local_key>

# Device-initiated unbinding: paired with Activation, placed alongside the Activation examples
./build/activate_demo <token> --release
```

> All of the AI demos above have built-in default device credentials and can be run directly; to use your own device, most demos accept trailing `[devid] [secret_key] [local_key]` arguments (`agent_trigger_demo` uses named arguments, see `--help`). `audio_chat_demo` is compiled only when libopus is detected.
