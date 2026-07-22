# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Observability — diagnostic logging on previously-silent error paths (core modules + PAL).
  PAL now logs `errno` on socket failures, and `tls_read`/`tls_write` log the raw
  mbedTLS `-0xXXXX` cause instead of collapsing to `TLS_ERR_NET`, so a
  `worker: recv error -3` is traceable across pal → tls → client. Also covers
  swallowed `malloc`/cJSON/crypto/frame-decode paths in iot-client, rtc-tcp-client,
  tuya-ble and common. Log-only — no behaviour change.

### Fixed

- iot-client — US region renamed to AZ(#7).
  - The IoT DNS region string and token prefix for the US West (Oregon) data
    center is `AZ`, not `US`. The enum member `US` is renamed to `AZ`,
    `iot_region_to_string()` now returns `"AZ"`, and the host macros
    `IOT_US_HOST` / `IOT_US_PRE_HOST` are renamed to `IOT_AZ_HOST` /
    `IOT_AZ_PRE_HOST` (values unchanged). NVS-stored region integers are
    unaffected (enum ordinal 1 is preserved).

## [0.3.0] - 2026-07-13

### Added

- IoT Client — OTA firmware upgrade support(#3).
  - `iot_ota_report_version` — reports the device's current firmware version
    (auto-called during `iot_client_init` from `iot_client_config_t.sw_ver`, so
    the cloud can evaluate upgrades against the running version).
  - `iot_ota_check_upgrade` — queries the cloud for a pending upgrade and
    returns version, download URL (`cdnUrl` preferred, `httpsUrl` fallback),
    file size, and MD5/HMAC hashes. Takes no `sw_ver` argument — the cloud
    compares against the version already reported at init.
  - `iot_ota_report_status` — drives the upgrade lifecycle
    (UPGRADING → FINI / EXEC / ABORT).
- RTC TCP Client (`tuya_ai`) — received images delivered via `on_image` callback(#6).
  - New `tai_image_msg_t` + `on_image` callback (message-struct API); format/width/height parsed from image-params on START/ONE_SHOT.
  - `TAI_PKT_IMAGE` now handled in `tai_proto_dispatch` (was dropped as unknown) — strips the 8-byte media header and emits each chunk for the caller to reassemble START..END / ONE_SHOT.

### Changed

- PAL / common — TCP connect timeout(#1).
  - `tcp_connect` takes a `timeout_ms`, bounded by `select()` (0 = single non-blocking attempt).
  - Threaded through all call sites (ai-tcp, HTTP, MQTT's new `MQTT_CONNECT_TIMEOUT_MS`).
  - `tls_config_t`'s two timeouts collapse into `connect_timeout_ms` — one deadline for TCP connect + TLS handshake (removes `handshake_timeout_ms`).
- common / TLS — ESP-IDF cert-bundle decoupled via callback(#2).
- iot-client / common — memory-management pass(#4).
  - `iot_client_t` inlines `https_url`/`mqtt_url` as `char[64]` and the DP context as inline storage (were `strdup` / lazy `malloc`), `_Static_assert`-guarded.
  - Per-op allocations removed: stacked atop sign buffer + MQTT subscribe/publish topics; HTTP request-header and response share one allocation; DP report/state returns the cJSON string directly; `tai_pkt_log` formats into a stack buffer.
  - mbedTLS global config (allocator, record sizes) left to the integrator — removed the SDK-side `MBEDTLS_USER_CONFIG_FILE` wiring, ownership documented in `common/tls.h`.
- RTC TCP Client (`tuya_ai`) — protocol-layer redesign.
  - Receive callbacks are now struct-based (ABI break).
  - Downstream audio-params re-read per stream.
  - Scatter-gather streaming send.
  - Smaller receive buffers via a smaller max fragment.

### Fixed

- RTC TCP Client (`tuya_ai`) — protocol-layer redesign.
  - `tai_disconnect` holds the send lock across the transport close (fixes a use-after-free).
  - Unknown downstream Packet/Event types are tolerated (logged + skipped); malformed packets stay fail-fast.
  - Oversized inbound frame is fail-fast (`TAI_PROTO_ERR_OVERSIZED`) instead of stalling to the liveness timeout.
  - `on_disconnect` fires at most once per Connection, for terminal disconnects only.
  - `on_disconnect` is no longer fired on the connecting thread.
  - Unsigned ClientHello framed into a `TAI_TX_CTRL_BUF_SIZE` buffer (was a fixed 256 B).
  - A send failure after committing bytes no longer rolls back the sequence number.
  - `gen_id` fails cleanly when the RNG errors, instead of emitting an uninitialized id.
  - Graceful peer TLS close (`close_notify`) surfaced as a comms error, detected immediately.
  - Shared process-wide RNG (`rng_bytes`) serialized by a pal mutex (fixes a CTR-DRBG data race).
  - `iot_init` now fails if RNG seeding fails.
  - `tai_connect` completes on the server's `AuthenticateResponse` (type 3), not only a `SessionNew` ack.
  - `tai_disconnect` returns promptly on an idle link (worker poll capped by `TAI_WORKER_POLL_CAP_MS`).
- PAL (FreeRTOS) — allocator mismatch: the worker-thread struct is freed with `pal_free`, not `vPortFree`.
- atop — `atop_activate_request` stack-allocates its response like the other ATOP calls, fixing an OOM leak of the POST-body buffer on the response-malloc-failure path.
- common / TLS — hardening follow-ups.
  - Restored human-readable X.509 verification diagnostics (`mbedtls_x509_crt_verify_info`).
  - `rng_bytes()` / `pv23_encrypt()` reject a NULL `pal`; redundant `memset`/`strlen` dropped in `tls_connect`.

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
- **MCP `mcp_example` example** demonstrating MCP tool integration with the agentic-kit
  framework.

[0.3.0]: https://github.com/tuya/agentic-kit/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/tuya/agentic-kit/compare/v0.1.0...v0.2.0
