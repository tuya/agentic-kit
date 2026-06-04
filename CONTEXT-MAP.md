# Context Map

The agentic-kit SDK spans several device-side contexts. Each owns its own language
and decisions.

## Contexts

- [IoT Client](./modules/iot-client/CONTEXT.md) — device ↔ Tuya cloud: activation, the
  Data Point model, MQTT reporting/downlink. ADRs: `modules/iot-client/docs/adr/`.
- _rtc-tcp-client_ (`modules/rtc-tcp-client/`) — Tuya AI Foundation real-time
  audio/video/text transport. _Not yet documented._
- _tuya-ble_ (`modules/tuya-ble/`) — BLE provisioning (WiFi credential exchange).
  _Not yet documented._

## Relationships

- **IoT Client → rtc-tcp-client**: the IoT Client obtains an AI session token
  (`iot_client_get_session_token`) that the RTC client uses to open a session.
- All contexts share the Platform Abstraction Layer (`pal/pal.h`) for sockets, memory,
  mutex, threads, and time.
