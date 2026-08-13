# Context Map

The agentic-kit SDK spans several device-side contexts. Each owns its own language
and decisions.

## Contexts

- [IoT Client](./modules/iot-client/CONTEXT.md) — device ↔ Tuya cloud: activation, the
  Data Point model, MQTT reporting/downlink. ADRs: `modules/iot-client/docs/adr/`.
- [RTC TCP Client](./modules/rtc-tcp-client/CONTEXT.md) — device ↔ Tuya AI Foundation
  real-time audio/video/text transport: the Connection, Session, Event lifecycle and the
  binary packet/frame framing. Design decisions live in CONTEXT.md and the callback-contract
  block in `modules/rtc-tcp-client/include/tuya_ai.h`.
- [Tuya BLE](./modules/tuya-ble/CONTEXT.md) — device ↔ app BLE provisioning: advertising,
  the pairing handshake, and WiFi-credential delivery.

## Relationships

- **IoT Client → RTC TCP Client**: the IoT Client obtains an AI session token
  (`iot_client_get_session_token`) that the RTC client uses to open a Session.
  The IoT Client also delivers server-initiated AI interrupts (e.g. `asrInterrupt`)
  via MQTT protocol 9000 to a registered `ai_ctrl_callback` — a standalone control
  channel independent of the TCP data stream's congestion state (see
  `spec/mqtt-control-channel-plan.md`).
- **Tuya BLE → IoT Client**: BLE provisioning runs first and hands the device its WiFi
  credentials (incl. the pairing token); once on WiFi, the IoT Client uses them to activate
  against the cloud.
- All contexts share the Platform Abstraction Layer (`pal/pal.h`) for sockets, memory,
  mutex, threads, and time.
