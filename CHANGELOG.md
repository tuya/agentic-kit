# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- iot-client — generic ATOP call (`iot_atop_call`), for cloud interfaces the SDK
  does not wrap by name.
  - New public header `iot_atop.h`: pass an `api` name, its `version` and a JSON
    request body; get the envelope's `result` back as a JSON string, plus the
    cloud's `errorCode` / `errorMsg` and server time. Signing, AES-GCM body
    encryption, TLS, host resolution and envelope parsing stay inside the SDK,
    so the caller never handles the device secret key.
  - Credentials and endpoint come from `iot_client_t` (same pattern as
    `iot_ota.c`), so the call covers activated devices only — it signs with
    `devid` + `secret_key` and returns `OPRT_UNINITIALIZED` before activation.
    Activation itself uses `uuid` + `authkey` and stays behind
    `iot_client_init_on_boarding()`.
  - `result` is returned as a JSON string rather than a `cJSON *`, keeping cJSON
    out of the public ABI and the memory ownership on one side
    (`iot_atop_response_free`). Request bodies are forwarded verbatim — callers
    supply whatever the interface needs, including the `t` field most ATOP
    interfaces expect in the body — and are validated only as far as "parses as
    a JSON object", to turn a typo into a local error instead of a round trip.
  - New guide `docs-site/docs/guides/atop-generic-call.md` lists the existing
    named wrappers (so they are not reimplemented) and the three criteria for
    promoting an interface to a named wrapper: used by more than one product,
    non-trivial protocol semantics, or needs SDK-internal state.
  - `modules/iot-client/CONTEXT.md` gains the ATOP vocabulary it was missing —
    *ATOP interface*, *envelope*, *named wrapper*, *generic call* — and flags
    "封装/wrap" as the ambiguity that made the design discussion circle.

- iot-client — cloud device-remove (protocol 11) callback.
  - New `iot_reset_callback_t` and `iot_reset_type_t` in `iot_client.h`;
    `iot_client_config_t` / `iot_on_boarding_config_t` / `iot_client_t` carry
    `reset_callback` and `reset_user_data` fields, forwarded through both
    on-boarding paths.
  - When the cloud pushes a `{"protocol":11,…}` MQTT notice (user removed the
    device or ordered a factory reset), the message layer classifies it by the
    root-level `"type"` field (`"reset_factory"` → `IOT_RESET_REMOTE_FACTORY`,
    else `IOT_RESET_REMOTE_UNBIND`) and fires the callback. Consumption is
    opt-in: with a `reset_callback` registered the notice never reaches the
    DP layer or the raw `message_callback`; without one it stays on the
    `message_callback` path exactly as in earlier releases. Works in loose
    mode (no schema). Mirrors TuyaOpen's `mqtt_service_reset_cmd_on`
    (`tuya_iot.c:301-328`).
  - The `dp_management_demo` registers the callback and, on fire, wipes
    `dp_state.json` / `schema.json` and exits, pointing at `pair/scan-by-app`
    for re-pairing. The SDK fires the event; the app owns storage wipe and
    pairing restart — no `tal_kv` / state machine is added.

- Examples — device-unbind demo (`unbind_demo`).
  - New POSIX example `examples/posix/pair/unbind-demo/` that connects with
    existing credentials, registers `reset_callback`, and waits for the cloud
    device-remove notice, printing whether it was an unbind or a factory
    reset. Registered as the `unbind_demo` target in
    `examples/posix/CMakeLists.txt`.

- iot-client — OTA firmware digest verification (`iot_ota_verify_*`).
  - New streaming verifier API `iot_ota_verify_init` / `iot_ota_verify_update` /
    `iot_ota_verify_finish` / `iot_ota_verify_abort` in `iot_ota.h`: the
    application feeds downloaded firmware chunks into the context while
    flashing, and `iot_ota_verify_finish()` compares the result against the
    cloud-provided digest from `iot_ota_check_upgrade()`.
  - Algorithm matches TuyaOpen's `tuya_ota.c`: when `hmac` is present the
    expected value is `HMAC-SHA256(device secret_key,
    UPPERCASE_hex(SHA-256(image)))` (the HMAC message is the 64-char
    uppercase hex string of the SHA-256 digest, matching TuyaOpen's `hex2str`);
    when only `md5` is present it falls back to plain MD5. Comparison is
    case-insensitive. Neither digest present → `OPRT_NOT_SUPPORTED`;
    mismatch → the new error code `OPRT_OTA_VERIFY_FAILED (-0x000D)`.
  - An empty digest string counts as absent, so a blank `hmac` falls through
    to a usable `md5` instead of failing the upgrade; a non-empty `hmac` of
    the wrong length is still rejected rather than downgraded to `md5`.
    `iot_ota_verify_init` does not write `*ctx_out` on any error path
    (initialize it to NULL), so the skip path is `if (ctx != NULL)`;
    `finish(NULL)` is a parameter error, not "skip".
  - Both OTA demos now verify: the ESP-IDF demo checks the digest before
    `esp_ota_set_boot_partition` and aborts on mismatch; the POSIX demo
    re-reads the downloaded file, checking `ferror` so a read error is not
    reported as a digest mismatch. Unit tests with Python-generated
    known-answer vectors added as `iot_ota_verify_test`.

- Examples — agent trigger demo (`tai_agent_trigger_demo`).
  - New POSIX example `examples/posix/ai/rtc-tcp-client/agent_trigger_demo.c`
    for cloud-initiated pushes: it runs both halves of the link at once —
    iot-client reports the DPs a device event rule reads, rtc-tcp-client holds
    an idle AI session for the agent's message to land on. It never calls
    `tai_send_text()`, so every turn it prints is one the server started.
    Registered as the `tai_agent_trigger_demo` target in
    `examples/posix/CMakeLists.txt`.
  - The session is opened before the DP report, and the demo reports a healthy
    baseline (battery 99), then a random value inside the DP's range, before
    the value that fires the rule (battery 5): a rule fires on a transition,
    and a trigger that fires while the device holds no session has nowhere to
    push to. `--no-baseline` skips the first report, `--no-mid` the middle one,
    `--listen` skips them all.
  - The device, its product schema and the trigger DP are compiled in
    (`DEFAULT_*`), with no flags to override them: the demo only means anything
    against the product whose rule and trigger were configured for it. The
    trigger DP's type and range still come from that schema, so editing it for
    another product needs no change to the reporting code.
  - A run that received nothing exits after printing the cloud-side
    configuration to check, since the device half is verifiable from its own
    log. Dropped text streams fail the run — one of them may have been the
    pushed message.
  - User guide added at `docs-site/docs/tutorials/agent-trigger.md`, covering
    the platform-side event rule and agent trigger setup that must exist
    before the demo can print anything.

- Examples — music play demo (`tai_music_play_demo`)(#15).
  - New POSIX example `examples/posix/ai/rtc-tcp-client/music_play_demo.c` that
    sends a text query triggering the server's music skill, parses the returned
    audio metadata (artist / album / song / url), prints it, and downloads the
    mp3 trial clip. Registered as the `tai_music_play_demo` target in
    `examples/posix/CMakeLists.txt`.
  - The trial clip is fetched with `fork` + `execvp`, never through a shell, so
    the server-supplied URL is one argv element that cannot be read as a
    command; only `http(s)` URLs are accepted. Credentials from `argv` are
    length-checked before they reach `iot_client_config_t`'s 32-byte fields.
  - `"code"` is read inside the SKILL envelope's `data` object — resolved
    against the whole document, the common `{"code":0,…,"data":{"code":"music"}}`
    shape would match the outer status code instead. The exit status reflects
    the outcome: a music response that cannot be read, or a download that
    fails, exits non-zero.
  - The metadata box is padded by display column rather than by byte, so the
    Chinese song titles this demo exists to show line up.
  - User guide added at `docs-site/docs/tutorials/music-play.md`, including
    copyright notes on trial-clip duration limits and NetEase Cloud Music
    integration via the Tuya content server.

- rtc-tcp-client — send an image and streamed audio as ONE multimodal event.
  - `tai_send_image_audio_start()` / `_chunk()` / `_end()` emit
    `EventStart -> Image(OneShot) -> Audio(START..MIDDLE..END) ->
    EventPayloadsEnd -> EventEnd`, so a spoken question *about a picture*
    arrives as a single turn. Composing it from the existing calls produces two
    separate events, leaving the server no way to know the speech refers to the
    image.
  - `_chunk` / `_end` keep `tai_send_audio_chunk` / `_end` semantics, so an
    existing audio loop only changes the call that opens the turn.
  - Covered by `test_image_audio_query()` in the loopback integration test.

- rtc-tcp-client — per-connection custom parameters on `EventStart`.
  - New `tai_config_t.event_custom_param_json`. When set, the event's user data
    gains a sibling to `chatAttributes`:
    `{"chatAttributes":"...","sessionAttributes":{"custom.param":<raw>}}`.
    Server-side workflows read device intent from `custom.param`, e.g.
    `{"clm_intent":"ai_image"}`. It is spliced in as a nested object rather than
    an escaped string because that is the shape the workflow expects.
  - `NULL` (the default) omits `sessionAttributes` entirely, so existing callers
    are unaffected.

- rtc-tcp-client — `tai_set_event_params()` changes the event parameters per
  turn. Without it the intent is fixed at `tai_ctx_init()` time, so a device
  that alternates between turn kinds — an image-recognition turn and a plain
  chat turn, say — cannot tell the server which one it is sending. Both
  arguments are borrowed, not copied, and must stay valid until the next call.

### Changed

- Examples — the rtc-tcp-client POSIX demos share their parsing helpers.
  - `demo_json.h` (JSON readers, base64, session-token parsing, bounded copy
    into fixed-size config fields), `demo_text.h` (`tai_text_msg_t` handling
    and stream reassembly) and `demo_mcp.h` (device-side MCP answering)
    replace the copy of that code each of the five demos carried, alongside
    the existing `demo_reconnect.h`.
  - The shared JSON readers are string- and escape-aware: a `{`, `}`, `[`, `]`
    or `"` inside a JSON string no longer terminates a span, and `\"`, `\/`
    and `\uXXXX` (including surrogate pairs) decode instead of truncating the
    value. A value that does not fit its buffer now reports failure rather than
    being silently truncated, and `parse_token` names the field when that
    happens — an empty `derived_client_id` / `agentToken` otherwise surfaced
    only as an unexplained auth failure. An out-of-range port is rejected
    instead of being truncated modulo 65536.
  - The music demo leaves `session_attrs_json` / `event_user_data_json` NULL
    instead of spelling out a subset of the built-in defaults. Setting either
    replaces the default wholesale rather than merging, so the subset was
    silently dropping `tts.order.supports`, `asr.enableVad`, `tts.alternate`
    and `processing.interrupt`.

- iot-client — `iot_get_qrcode_info` and `iot_get_ca_certificate` now write
  into caller-provided buffers (API break)(#10).
  - The single-field `iot_qrcode_response_t` struct is removed and both APIs
    take `char *buf, size_t buf_len` instead of returning pal-allocated
    strings — no heap ownership passes to the caller, matching the
    `iot_client_get_session_token` convention. Both return
    `OPRT_INVALID_RESULT` if the buffer is too small.
  - `iot_get_ca_certificate` now also returns `OPRT_INVALID_RESULT` when the
    server has no CA certificate for the endpoint (previously an empty string
    was silently returned as success).

- Observability — diagnostic logging on previously-silent error paths (core modules + PAL)(#11).
  PAL now logs `errno` on socket failures, and `tls_read`/`tls_write` log the raw
  mbedTLS `-0xXXXX` cause instead of collapsing to `TLS_ERR_NET`, so a
  `worker: recv error -3` is traceable across pal → tls → client. Also covers
  swallowed `malloc`/cJSON/crypto/frame-decode paths in iot-client, rtc-tcp-client,
  tuya-ble and common. Log-only — no behaviour change.

### Fixed

- common/tls — a TLS 1.3 `NewSessionTicket` tore the session down. A server may
  deliver one between application records; mbedTLS surfaces it to the reader as
  `MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET`, a non-fatal "read again"
  signal, but `tls_read()` fell through to its generic error branch. Observed as
  a session dropping mid-stream at random, with nothing in the log to tell it
  apart from a real transport failure. Guarded by `#ifdef` so the code still
  builds against mbedTLS versions predating the constant.

- iot-client — a cloud-rejected ATOP call reported success. When the envelope
  carried `success: false`, `atop_response_result_parse_cjson()` logged
  `errorCode` / `errorMsg` and dropped them, then returned `OPRT_OK` for every
  code except `GATEWAY_NOT_EXISTS`. Callers had to inspect
  `atop_base_response_t.success` separately, and only three of the eight named
  wrappers did.
  - Worst case was `tuya.device.schema.newest.get`: a rejection came back as
    `OPRT_OK` with `result == NULL`, which `atop_schema_newest_get()` reports as
    "no newer schema" — a permission error and an up-to-date schema were
    indistinguishable from the return value.
  - The envelope now copies both strings into new `error_code` / `error_msg`
    fields on `atop_base_response_t` and returns the new
    `OPRT_ATOP_BUSINESS_ERROR (-0x000E)` for every rejection uniformly —
    including `GATEWAY_NOT_EXISTS`, whose historical `OPRT_COMMUNICATION_ERROR`
    mapping protected no caller (nothing in the repo branches on it) while
    presenting a permanent "device removed" verdict as a retryable transport
    failure. A caller that needs per-code policy branches on `error_code`.
  - Behavioral note: named wrappers now return `-0x000E` instead of the old
    `OPRT_OK`-with-empty-result (or, for two wrappers, an internal
    `OPRT_COMMUNICATION_ERROR` mapping) when the cloud rejects a call. The
    activation error log names the new code; the two now-unreachable
    `.success` re-checks in `atop.c` were removed.
  - `atop_base_response_free()` is now NULL-safe and frees `result` whenever it
    is set instead of gating on `.success`, which only ever added a way to leak.
    A rejection whose envelope lacks `errorCode` still logs the server's
    `errorMsg` (previously that text was dropped on this path).

- iot-client — US-East (`UEAZ`) and West-Europe (`WEAZ`) never resolved an MQTT
  broker(#15). `iot_region_to_string()` sent the enum name as the `region` field
  of `POST /v2/url_config`, but the service knows those two by their two-letter
  activation-token prefix, `UE` and `WE`. The other five regions were correct.
  - It failed silently: an unknown region is answered with HTTP 200 minus the
    endpoint objects, so the query returned `OPRT_OK` and `mqtt_url` stayed empty
    with no log line, surfacing only as a refused MQTT connect. A missing
    endpoint now warns — which is also how the one remaining gap shows up:
    West-Europe publishes no `httpsUrl`, so ATOP there still falls back to the
    compile-time `IOT_WEAZ_HOST`.
  - The queried keys are now `IOT_DNS_KEY_*` constants in `iot_dns.h` rather than
    literals repeated across the three query sites, each of which also has to
    compare the key back against the response.
  - `iot_region_to_string()` moved to `iot_dns.c` beside `iot_region_to_host()`,
    and `dns_test.c` pins the table as the inverse of `__token_to_region()` in
    `iot_on_boarding.c` — those drifting apart is what caused this.

- Examples — the tool-less rtc-tcp-client demos answer MCP requests correctly.
  text_chat, audio_chat, edu_camera and music_play each answered every
  `TAI_EVT_MCP_CMD` with one canned reply that hardcoded `"id":1` — JSON-RPC
  correlates a response to its request by echoing the id — and always used the
  `tools/call` result shape, so the `initialize` handshake and `tools/list`
  were answered with the wrong body. Opting out is not an option: the SDK's
  built-in default session attributes declare `deviceMcp.supportCustomMCP`, so
  a device that passes no `session_attrs_json` is asked anyway.
  - New `demo_mcp.h` answers as a device with an empty tool catalog: it echoes
    the request id, returns the right result shape per method
    (`initialize` / `tools/list` / `tools/call`), reports unknown methods as
    JSON-RPC `-32601`, and stays silent for a request with no id, which is a
    notification. Its `demo_mcp_copy_id()` also replaces `mcp_demo`'s local
    copy, where an id too long for the buffer used to be spliced in truncated
    — dropping its closing quote and producing unparseable JSON.
  - `id` and `method` are read from the request's top-level members only: a
    `tools/call` may carry an `"id"` of its own inside `params.arguments`,
    which a document-order search finds first. An object or array id is
    refused rather than spliced back unbalanced.
  - `mcp_demo` implements real tools, and now stays silent for notifications
    instead of answering `"id":null`.

- Examples — text streams that cannot be reassembled are reported, not dropped
  in silence. Each loss is counted in `demo_textbuf_t.dropped`, and
  `music_play_demo` exits non-zero on it rather than reporting "no music skill
  response" and exiting 0 for a run that lost its payload. A stream displaced by
  a new `START` used to vanish without a word. A `seq` gap now warns and keeps
  accumulating — the empty frames the SDK swallows consume a seq while carrying
  no bytes, so continuing reassembles the right document where dropping loses a
  healthy one; `-DDEMO_TEXT_SEQ_CHECK=2` drops instead, `=0` skips the check.

- Examples — NLG prose is unescaped before printing. `nlg_print_content()`
  decodes `\n` and `\uXXXX` (Chinese arrived on the terminal as escapes) and
  claims the empty terminator line `{"content":""}`, which used to fall through
  and dump a whole JSON envelope into the middle of the prose.

- Examples — a value too long for a field that is only printed truncates
  instead of being emptied: `music_play_demo` showed `Song: (unknown)` for a
  title past 255 bytes and dropped long cover URLs entirely. Credentials still
  reject. `parse_token` tells capacity apart from a wrong type and a bad escape,
  and an audio URL that does not fit is a parse failure, not a success with no
  URL.

- Examples — out-of-bounds read on received text in the rtc-tcp-client demos.
  `tai_text_msg_t.text` is a borrowed slice of the SDK receive buffer and is not
  NUL-terminated, but the demos ran `strstr`/`strchr` over it — reading past
  `msg->len` into the previous packet's bytes and, eventually, past the end of
  the `tai_ctx_t` allocation. All text handling is now length-bounded or copies
  the bytes out first. Reassembly also lets the music demo recognise a SKILL
  response split across `TAI_STREAM_START`/`MIDDLE`/`END` — parsed per chunk
  it never matched at all.

- Examples — stack overflow from `argv` credentials in the rtc-tcp-client demos.
  `devid` / `secret_key` / `local_key` were `memcpy`'d into
  `iot_client_config_t`'s 32-byte fields with no length check, so an over-long
  value overwrote the adjacent fields and ran past the end of the stack-local
  config. All five demos now go through `demo_copy_field()`, which rejects a
  value that does not fit.

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
