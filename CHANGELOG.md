# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- iot-client — US-East (`UEAZ`) fell back to an ATOP host that does not resolve(#31).
  `IOT_UEAZ_HOST` is now `a1-ueaz.tuyaus.com`.

## [0.4.0] - 2026-08-27

### Added

- iot-client — device-initiated reset (`iot_client_reset`), for a device that
  unbinds itself instead of waiting to be removed from the app(#27).
  - `OPRT_OK` destroys the client; any other code leaves it usable so the call
    can be retried.
  - `iot_reset_scope_t` chooses how much the cloud clears(#28):
    `IOT_RESET_UNBIND_ONLY` drops the binding and keeps the device's cloud-side
    data, `IOT_RESET_FACTORY` also discards that data and **cannot be undone**.
  - The optional `error_code` out-param carries the cloud's rejection reason —
    needed to tell a terminal `GATEWAY_NOT_EXISTS` (wipe credentials, re-pair)
    from a retryable `REMOTE_API_RUN_UNKNOW_FAILED`.
  - It does not erase persisted credentials/DP state/schema; that stays the
    app's job. Shown in `pair/api-activate --release`.
- iot-client — `iot_client_get_session_token_ex()` reports *why* the cloud
  refused an agent token(#29). The refusals need opposite handling — a terminal
  `GATEWAY_NOT_EXISTS` (device removed; re-pair) versus retryable ones — and the
  plain function cannot tell them apart.
- iot-client — `iot_client_connect()` / `iot_client_disconnect()` are public(#26).
  A reconnect loop previously had to reach into `src/iot_client_message.h`,
  which is not installed.
- iot-client — generic ATOP call (`iot_atop_call`), for cloud interfaces the SDK
  does not wrap by name(#19).
  - New public header `iot_atop.h`: pass an `api` name, its `version` and a JSON
    body; get the envelope's `result` back as a JSON string plus the cloud's
    `errorCode` / `errorMsg`. Activated devices only.
  - `docs-site/docs/guides/atop-generic-call.md` lists the named wrappers and
    when to promote an interface to one.
- iot-client — cloud device-remove (protocol 11) callback(#18).
  - New `iot_reset_callback_t` / `iot_reset_type_t`; registering it makes the
    SDK consume protocol-11 notices instead of passing them to
    `message_callback`.
- iot-client — OTA firmware digest verification (`iot_ota_verify_*`)(#17).
  - Streaming verifier (`init` / `update` / `finish` / `abort`) so a download can
    be checked without buffering the image.
- common — coreMQTT and coreHTTP Error/Warn logs are routed into the SDK log
  facade (`common/core_{mqtt,http}_config.h`)(#24, #25).
  - A refused CONNECT now names the reason (`Connection refused: bad user name
    or password.`) instead of a bare `MQTT_Connect failed: 6`, and an oversized
    ATOP response says `insufficient space: responseBufferLen=...`.
  - `mqtt.c`'s own log lines print the symbolic `MQTTStatus_t` name alongside the
    number. `docs-site/docs/reference/iot-client.md` gains status and CONNACK
    tables for reading older logs.
  - Do not set `MQTT_DO_NOT_USE_CUSTOM_CONFIG` / `HTTP_DO_NOT_USE_CUSTOM_CONFIG`
    again — in either build path — or the reasons go silent.
- iot-client — APP-confirmed OTA notice (protocol 15) callback(#21).
  - New `iot_ota_confirm_callback_t` on both config structs: the cloud pushes it
    once the user confirms the upgrade in the app, with the firmware channel.
    Registering it makes the SDK consume protocol-15 notices.
- Examples — device-unbind demo (`unbind_demo`)(#18).
- Examples — agent trigger demo (`tai_agent_trigger_demo`)(#22).
- Examples — music play demo (`tai_music_play_demo`)(#12).
- rtc-tcp-client — send an image and streamed audio as ONE multimodal event
  (`tai_send_image_audio_start()` / `_chunk()` / `_end()`)(#20).
- rtc-tcp-client — per-connection custom parameters on `EventStart` via
  `tai_config_t.event_custom_param_json`(#20).
- rtc-tcp-client — `tai_set_event_params()` changes event parameters per turn,
  for devices that alternate between turn kinds(#20).

### Changed

- **BREAKING** iot-client — MQTT auto-connect is on by default;
  `mqtt_auto_connect` becomes `mqtt_disable_auto_connect` on both config
  structs(#26).
  - Migration: `.mqtt_auto_connect = false` → `.mqtt_disable_auto_connect = true`;
    `.mqtt_auto_connect = true` can be deleted. The rename means old code fails
    to compile rather than silently changing behaviour.
  - A failed auto-connect still returns `NULL` from `iot_client_init()`. Devices
    that may boot without a network, or that use ATOP over HTTP with no broker,
    must now opt out and drive the link themselves.
- **BREAKING** iot-client — `iot_get_qrcode_info` and `iot_get_ca_certificate`
  write into caller-provided buffers; `iot_qrcode_response_t` is removed(#10).
- **BREAKING** Examples — every POSIX example binary is named after its source
  file. The rtc-client (prebuilt UDP lib) demos gain a `udp_` prefix:
  `chat_demo` / `edu_camera_demo` become `udp_chat_demo` / `udp_edu_camera_demo`.
- Docs — the cloud-VAD turn boundary is `TAI_EVT_CHAT_BREAK`, not
  `TAI_EVT_SERVER_VAD`.
  - The cloud no longer sends `TAI_EVT_SERVER_VAD` (type 5) in cloud-VAD mode; it
    ends a user turn with an inbound `TAI_EVT_CHAT_BREAK` (type 4). The constant
    is kept for protocol compatibility but marked legacy.
- Observability — diagnostic logging on previously-silent error paths (core
  modules + PAL)(#11). PAL logs `errno` on socket failures; `tls_read`/`tls_write`
  log the raw mbedTLS cause instead of collapsing to `TLS_ERR_NET`.
- Examples — the rtc-tcp-client POSIX demos share their parsing helpers
  (`demo_json.h`, `demo_text.h`)(#14).

### Fixed

- iot-client — a cloud-rejected ATOP call reported success(#19). A `success: false`
  envelope logged `errorCode` / `errorMsg`, dropped them and returned `OPRT_OK`;
  it now returns `OPRT_ATOP_BUSINESS_ERROR` uniformly, with both strings on the
  response. Callers needing per-code policy branch on `error_code`.
- iot-client — a peer that closed a non-TLS MQTT connection went unnoticed until
  the 60 s keepalive expired(#26): `transport_recv()` passed a 0 (EOF per `pal.h`)
  straight to coreMQTT, which reads it as "no data yet".
- iot-client — tearing down an already-dead link no longer pushes a DISCONNECT
  into a socket that cannot carry it(#26). Only `MQTTRecvFailed` / `MQTTSendFailed` /
  `MQTTKeepAliveTimeout` count as dead; a completed exchange clears the flag.
- iot-client — `iot_client_deinit()` wipes the client before freeing it(#27). `devid`,
  `secret_key` and `local_key` are plaintext in `iot_client_t`, and an embedded
  allocator hands the block to whatever mallocs next.
- iot-client — US-East (`UEAZ`) and West-Europe (`WEAZ`) never resolved an MQTT
  broker(#15). `iot_region_to_string()` sent the enum name where the service
  expects the two-letter activation-token prefix.
- iot-client — an ATOP response larger than the buffer reported a generic
  communication error(#23). `REQUEST_HEADER_BUFFER_SIZE` and
  `RESPONSE_BUFFER_SIZE` are now `#ifndef`-guarded so an application can size
  them with `-D`, and `HTTPInsufficientMemory` logs what was needed versus what
  the buffer holds. A product whose schema came back at contentLength 6558
  against the 4096 default looked like a network fault.
- iot-client — SG region no longer falls back to the China ATOP host(#9).
- iot-client — US region renamed to `AZ`(#7). The DNS region string and token
  prefix for US West (Oregon) is `AZ`, not `US`.
- common/tls — a TLS 1.3 `NewSessionTicket` tore the session down(#20).
  `MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET` is a non-fatal "read again"
  signal that `tls_read()` treated as an error.
- Examples — `unbind-demo --reset` no longer requires a working MQTT connection(#27).
  Reset travels over ATOP HTTPS, and gating it on CONNECT disabled the flag in
  the one case it exists for: a device the cloud has already unbound.
- Examples — `audio_chat_demo` could not parse its session token on hosts where
  `/opt/homebrew/include` holds mbedtls 4.x headers: that directory lands first
  on this one target's include path (via `find_path(opus)`), shadowing the
  vendored 3.6.6 headers.
- Examples — `audio_chat_demo` sends its one-shot file audio under the device-VAD
  attributes it actually implements (`asr.enableVad`).
- Examples — the tool-less rtc-tcp-client demos answer MCP requests correctly:
  each reply now echoes the request id instead of a hardcoded `"id":1`(#14).
- Examples — text streams that cannot be reassembled are reported, not dropped
  silently(#14).
- Examples — NLG prose is unescaped before printing(#14).
- Examples — a value too long for a display-only field truncates instead of
  being emptied(#14).
- Examples — out-of-bounds read on received text: `tai_text_msg_t.text` is a
  borrowed, non-NUL-terminated slice(#14).
- Examples — stack overflow from over-long `argv` credentials(#14).

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
