# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **IoT Client — OTA firmware upgrade support** (`modules/iot-client/include/iot_ota.h`).
  High-level OTA API for version reporting, upgrade checks, and upgrade-status
  reporting, backed by three new ATOP service calls: `tuya.device.upgrade.get`
  (v4.4), `tuya.device.versions.update` (v4.1), and
  `tuya.device.upgrade.status.update` (v4.1).
  - `iot_ota_report_version` — reports the device's current firmware version
    (also auto-called during `iot_client_init`, so the cloud can evaluate
    upgrades).
  - `iot_ota_check_upgrade` — queries the cloud for a pending upgrade and
    returns version, download URL (`cdnUrl` preferred, `httpsUrl` fallback),
    file size, and MD5/HMAC hashes.
  - `iot_ota_report_status` — drives the upgrade lifecycle
    (UPGRADING → FINI / EXEC / ABORT).
  - The SDK handles **only the cloud protocol**; the application owns download
    and flash (e.g. ESP-IDF `esp_ota_*` or a vendor bootloader API).
  - `atop_base` gains an AES-128-ECB fallback decrypt path for older (`et=1`)
    cloud responses.
  - Unit tests with mocked ATOP endpoints (`iot_ota_test`), a POSIX `ota-demo`,
    and an ESP-IDF `ota-demo` with a two-partition OTA table.

### Changed

- **RTC TCP Client (`tuya_ai`) — receive callbacks are now struct-based (ABI break).**
  `on_audio` / `on_text` / `on_event` / `on_disconnect` each take a single const message
  pointer (`tai_audio_msg_t` / `tai_text_msg_t` / `tai_event_msg_t` / `tai_disconnect_msg_t`)
  instead of positional parameters. The structs surface previously-dropped metadata —
  `stream_flag`, `data_id`, `event_id`, `codec`, `seq`, `timestamp_ms` — and `on_disconnect`
  now distinguishes its source via `reason` (SESSION_CLOSE / CONNECTION_CLOSE / TRANSPORT /
  PROTOCOL) plus `detail`. Inner pointers are valid only for the callback's duration (copy to
  retain). Adds `tai_config_t.connect_timeout_ms` (reserved for confirmed connect). All
  consumers (examples + integration tests) updated. First phase of the `tuya_ai` protocol-layer
  redesign; fail-fast recovery and streaming send follow.

- **RTC TCP Client (`tuya_ai`) — downstream audio-params re-read per stream.** Audio parameters
  (`codec` / `sample_rate` / `frame_size`) are now re-parsed on each stream START / ONE_SHOT, so a
  second downstream audio stream no longer inherits the previous stream's `frame_size` or codec.
  (Fragmented Audio/Text is reassembled whole before the dispatcher splits it into CBR Opus frames,
  as before.)

- **RTC TCP Client (`tuya_ai`) — scatter-gather streaming send (~107 KB less RAM).**
  EVERY packet — media (audio chunks, images, large text, MCP JSON) and control — now streams
  through one scatter-gather sender instead of being assembled into a large contiguous frame
  buffer. For media only a small application header is built (≤256 B) and the payload is signed +
  written zero-copy from the caller's buffer; for control the whole packet (assembled in
  `tx_ctrl_buf`, whose attribute block can carry the session/event JSON — escaped JSON must fit
  `TAI_TX_CTRL_BUF_SIZE`, default 1 KB) is itself the zero-copy payload. The two 64 KB static TX
  buffers (`tx_app_buf` + `tx_frame_buf`) and the contiguous control frame buffer
  (`tx_ctrl_frame_buf`) are all gone — the context's send-side buffers drop from ~128 KB to
  ~1.3 KB — and the
  per-chunk payload `malloc` + triple copy become one small header copy plus a zero-copy payload
  write. The frame HMAC was generalised to sign a logical segment list (`tai_frame_hmac_sg`),
  byte-for-byte identical to the old contiguous signature (golden-matrix tested), so the wire
  format and the receiver are unchanged. ClientHello (sent unsigned, one-shot) is framed inline on
  the stack. Trade-offs (§6.6): each frame is 2–3 TLS records instead of one, and a send failure
  mid-frame desyncs the wire stream — it returns `TAI_ERR_NET` (a synchronous error the caller
  acts on: an app sender disconnects + reconnects; the worker's own periodic Ping turns a failed
  send into a TRANSPORT disconnect). Third phase of the `tuya_ai` redesign.

- **RTC TCP Client (`tuya_ai`) — smaller receive buffers via a smaller max fragment.**
  `TAI_MAX_FRAGMENT_PAYLOAD` is now 4 KB (was 32 KB) and is advertised to the server in ClientHello
  as `TAI_ATTR_MAX_FRAGMENT_LEN`. The RX sliding-window buffer is now *derived* —
  `TAI_RX_BUF_SIZE = TAI_MAX_FRAGMENT_PAYLOAD + 37` (exactly one max wire frame) — instead of a
  fixed 64 KB, and the fragment-reassembly buffer drops from 128 KB to 32 KB (the largest inbound
  packet the device accepts; a larger one is fail-fast `TAI_PROTO_ERR_FRAG`). Net: the context
  shrinks to ~37 KB (from ~320 KB at the start of the redesign). Caveat: `rx_buf` has zero
  headroom, so it relies on the server honouring the advertised `MAX_FRAGMENT_LEN`; an oversized
  inbound frame is now detected and fail-fast (`TAI_PROTO_ERR_OVERSIZED`) rather than stalling the
  receive loop until the liveness timeout. Tune `TAI_MAX_FRAGMENT_PAYLOAD` / `TAI_FRAG_BUF_SIZE`
  up for larger fragments / packets.

### Fixed

- **RTC TCP Client (`tuya_ai`) — code-review pass (concurrency, forward-compat, fail-fast).**
  - `tai_disconnect` now holds the send lock across the transport close (not just the
    SessionClose), so a concurrent sender on another thread can no longer use a TLS context this
    is freeing (use-after-free); the close nulls the handle under the lock, so a racing send fails
    cleanly.
  - **Unknown downstream Packet/Event types are now tolerated** (logged + skipped) instead of
    tearing the Connection down, so a server adding a forward-compatible new type does not knock
    existing clients into a reconnect storm. Malformed (decode-failure) packets are still fail-fast.
  - An oversized inbound frame (declared size > `rx_buf`) is now fail-fast
    (`TAI_PROTO_ERR_OVERSIZED`) instead of stalling until the liveness timeout.
  - `on_disconnect` is single-point for TERMINAL disconnects: a transport/protocol/connection-close
    cause fires it at most once per Connection. A non-terminal server SessionClose
    (`connection_alive=1`, the link may persist for a new session) is a distinct event — it does not
    latch the guard and does not suppress a later real transport death, so an app that keeps the link
    after a SessionClose is still told when the connection actually dies.
  - `on_disconnect` is no longer fired on the connecting thread: a server SessionClose received
    during `tai_connect`'s synchronous SessionNew-ack wait (before the worker exists) is surfaced via
    `tai_connect`'s return value instead of an out-of-band callback, honoring the "callbacks run on
    the worker thread" contract.
  - The unsigned ClientHello is now framed into a buffer sized to `TAI_TX_CTRL_BUF_SIZE` (was a fixed
    256 B), so a long `client_id`/`device_id` connects instead of failing `tai_connect` with a
    confusing buffer error; the only limit is the same control buffer the build step already enforces.
  - An app-thread send failure that fails *after* committing bytes (mid-fragment of a multi-fragment
    packet) no longer rolls back the sequence number — the seq is reclaimed only when nothing of the
    packet reached the wire, keeping the `TAI_ERR_NET`-means-committed contract intact.
  - Random hex IDs (`gen_id`) now fail the build cleanly if the RNG errors at runtime, instead of
    hex-encoding an uninitialized stack buffer into a wire id.
- **iot-client / common — TLS consolidation follow-ups.**
  - `mqtt` / `http` transport recv now surface a graceful peer TLS close (`tls_read` returns 0 on
    `close_notify`) as a communication error instead of an idle "no data" poll, so a server-initiated
    close is detected immediately rather than at the keepalive timeout.
  - The shared process-wide RNG (`rng_bytes`) is now serialized by a pal mutex, removing a data
    race on the single CTR-DRBG between subsystems on different threads (the consolidation collapsed
    three per-subsystem DRBGs into one). The mutex is created in `rng_init(const pal_t *)` and locked
    per call via `rng_bytes(const pal_t *, ...)` — both now take the pal, so the locking stays within
    the SDK's platform abstraction instead of calling an OS threading API from `common/`. Knock-on
    signature changes (each now carries the pal so it can reach the DRBG lock): `tai_random_bytes`,
    `pv23_encrypt` / `pv23_decrypt` gain a leading `const pal_t *` (encrypt locks the DRBG for its
    internally-generated IV — kept inside for GCM nonce misuse-resistance; decrypt takes it for
    symmetry and ignores it), and the TLS `f_rng` callback carries the pal via mbedTLS's `p_rng`.
  - `iot_init` now fails if RNG seeding fails (previously the error was swallowed and resurfaced
    later as opaque TLS/nonce errors).
- **PAL (FreeRTOS) — allocator mismatch.** The worker-thread struct allocated with `pal_malloc`
  (SPIRAM-capable on ESP-IDF) is now freed with `pal_free`, not `vPortFree`, so it is released back
  to the heap it came from.
- **RTC TCP Client (`tuya_ai`) — connect / disconnect robustness.**
  - `tai_connect` now completes on the server's `AuthenticateResponse` (packet type 3, carrying
    `connection-status-code` — 200 = OK), not only on a `SessionNew` ack. The production server
    confirms the handshake this way and immediately starts the session; the client previously fell
    through to the unknown-packet path and timed out (`SessionNew ack timeout` → `tai_connect`
    failed). The `SessionNew`-ack path is kept for compatibility.
  - `tai_disconnect` now returns promptly on an idle link. The receive worker's idle blocking read
    is capped (`TAI_WORKER_POLL_CAP_MS`) so it observes `running = 0` quickly, instead of waiting
    out the remainder of a full ping interval (up to ~60 s) before the join could return.
- **common / TLS — handshake bound & hardening follow-ups.**
  - The shared TLS handshake is now bounded by a deadline (`tls_config_t.handshake_timeout_ms`;
    rtc-tcp-client passes `connect_timeout_ms`, others use a default). A peer that completes the TCP
    connect but stalls the TLS handshake can no longer hang the caller indefinitely, and a socket
    error reported by `tcp_poll` during the handshake fails fast instead of spinning to the deadline.
  - Restored human-readable X.509 verification diagnostics (`mbedtls_x509_crt_verify_info`) that the
    per-module TLS logged before consolidation — a cert failure now reports the reason (CN mismatch /
    expired / untrusted chain) instead of only a hex flag.
  - `rng_bytes()` and `pv23_encrypt()` reject a NULL `pal` up front instead of dereferencing it, and
    the CA-PEM temp buffer in `tls_connect` drops a redundant `memset` / `strlen`.

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

[0.2.0]: https://github.com/tuya/agentic-kit/compare/v0.1.0...v0.2.0
