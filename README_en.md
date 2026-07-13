# Tuya Agentic-kit

Multimodal device-side SDK for connecting smart hardware to the Tuya AI platform. Supports voice chat, image understanding/generation, and device-side MCP.

## Features

- Voice/text conversation with low latency
- Image understanding and generation
- Device-side MCP (Model Context Protocol) support
- Device data point (DP) management: schema validation, local cache, batch reporting, downlink callbacks, state persistence
- OTA firmware upgrade: cloud protocol (version report / upgrade check / status report); download and flash owned by the application
- Platform and chip agnostic: macOS, Linux, FreeRTOS (ESP32), MIPS, ARM
- Global deployment with multiple data center regions

## SDK Modules

| Module | Header | Description |
|--------|--------|-------------|
| RTC TCP Client | `tuya_ai.h` | tRTC (Tuya RTC protocol), TCP implementation, fully open sourced with PAL portability |
| RTC Client | `stm_open.h` | tRTC (Tuya RTC protocol), UDP implementation, pre-compiled static library |
| IoT Client | `iot_client.h`, `iot_dp.h`, `iot_ota.h` | Device activation, MQTT, session token; Data Point (DP) management (schema validation / cache / up- & downlink / persistence); OTA firmware upgrade (cloud protocol) |
| Tuya BLE | `tuya_ble_nimble.h` | BLE provisioning (ESP-IDF) |

## Prerequisites

| Tool | Version | macOS | Linux (Debian/Ubuntu) |
|------|---------|-------|----------------------|
| CMake | >= 3.20 | `brew install cmake` | `apt install cmake` |

Bundled dependencies (mbedTLS, cJSON, coreHTTP, coreMQTT) are built automatically.

## Build

```sh
git clone https://github.com/tuya/agentic-kit.git
cd agentic-kit

git submodule update --init --recursive

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
./build/tai_text_chat_demo

# Device scan QR pairing
./build/scan_by_device_pair_demo

# App scan QR pairing
./build/scan_by_app_pair_demo

# Device data point (DP) management
./build/dp_management_demo

# OTA firmware upgrade (cloud protocol)
./build/ota_demo
```

## Project Structure

```
modules/
  rtc-tcp-client/     # tRTC TCP implementation (open source)
  rtc-client/         # tRTC UDP implementation (pre-compiled libs)
  iot-client/         # Device activation, MQTT, token, Data Point (DP) management
  tuya-ble/           # BLE provisioning
examples/
  posix/              # macOS/Linux examples
  esp-idf/            # ESP32 examples
pal/                  # Platform Abstraction Layer
common/               # Shared utilities and logging
third_party/          # Bundled third-party libraries
docs-site/            # Documentation website
```

## License

This project is licensed under the [Apache License 2.0](LICENSE).

**Exception**: The `modules/rtc-client/` module is **not open source**. The pre-compiled static libraries (`libstm.a`) and accompanying header files are proprietary assets of Tuya Inc. Clients are free to use, copy, modify, merge, publish, and distribute this module, including embedding it in hardware products (whether or not combined with agentic-kit). The **sole restriction** is that clients may **not** reverse engineer, decompile, or disassemble the pre-compiled libraries. See [modules/rtc-client/LICENSE](modules/rtc-client/LICENSE) for details.
