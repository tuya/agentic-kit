# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-06-12

### Added

- **IoT Client — Data Point (DP) management layer** (`modules/iot-client/include/iot_dp.h`).
  Sits on top of the existing transport: uplink reuses `iot_client_publish()`, downlink is
  intercepted on the `iot_client_process()` receive path — no new crypto path, threads, or timers.
  - Schema registry with typed validation (type / range / maxlen / enum) and a per-DP dirty
    bit; an empty or invalid schema falls back to loose pass-through mode.
  - Uplink reporting: `iot_dp_report` (single) and `iot_dp_report_all_dirty` /
    `iot_dp_report_all` (batched), guarded by the MQTT packet budget
    (`OPRT_DP_PAYLOAD_TOO_LARGE`).
  - Downlink protocol-5 DP-set dispatch with a per-DP change callback.
  - Persistence mechanism (the application decides when/where): `iot_dp_dump_json`,
    `iot_dp_validate_json` (strict) and `iot_dp_restore_json` (lenient); RAW DPs are reported
    uplink but never persisted.
  - Non-destructive schema upgrade via `iot_dp_schema_check_update()` (existing values kept,
    new DPs defaulted).
  - DP access mode (`ro`/`rw`/`wr`) from the product schema is treated as a cloud-side hint
    and is not enforced by the device SDK — the device may set/report any DP.
- **POSIX `dp_management_demo` example** demonstrating the DP layer end to end (connect,
  restore, report, downlink, persist).

[0.2.0]: https://github.com/tuya/agentic-kit/compare/v0.1.0...v0.2.0
