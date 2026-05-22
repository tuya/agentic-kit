# Tuya Agentic-kit

Multimodal device-side SDK for connecting smart hardware to the Tuya AI platform. Supports voice chat, image understanding/generation, and device-side MCP.

## Features

- Voice/text conversation with low latency
- Image understanding and generation
- Device-side MCP (Model Context Protocol) support
- Platform and chip agnostic: macOS, Linux, FreeRTOS (ESP32), MIPS, ARM
- Global deployment with multiple data center regions

## SDK Modules

| Module | Header | Description |
|--------|--------|-------------|
| RTC TCP Client | `tuya_ai.h` | tRTC (Tuya RTC protocol), TCP implementation, fullly open sourced with PAL portability |
| RTC Client | `stm_open.h` | tRTC (Tuya RTC protocol), UDP implementation, pre-compiled static library |
| IoT Client | `iot_client.h` | Device activation, MQTT, session token |
| Tuya BLE | `tuya_ble_nimble.h` | BLE provisioning (ESP-IDF) |

## Prerequisites

| Tool | Version | macOS | Linux (Debian/Ubuntu) |
|------|---------|-------|----------------------|
| CMake | >= 3.20 | `brew install cmake` | `apt install cmake` |

Bundled dependencies (mbedTLS, cJSON, coreHTTP, coreMQTT) are built automatically.

## Build

```sh
mkdir -p build && cd build
cmake .. && make
```
## Run Examples

Build the posix examples:

```sh
cmake -S examples/posix -B build
cmake --build build
```

Then run:

```sh
# Voice/text chat (RTC TCP Client)
./build/tai_chat_demo

# Device scan QR pairing
./build/scan_by_device_pair_demo

# App scan QR pairing
./build/scan_by_app_pair_demo
```

## Project Structure

```
modules/
  rtc-tcp-client/     # tRTC TCP implementation (source, recommended)
  rtc-client/         # tRTC UDP implementation (pre-compiled libs)
  iot-client/         # Device activation, MQTT, token
  tuya-ble/           # BLE provisioning
examples/
  posix/              # macOS/Linux examples
  esp-idf/            # ESP32 examples
pal/                  # Platform Abstraction Layer
common/               # Shared utilities and logging
third_party/          # Bundled third-party libraries
docs-site/            # Documentation website
```
