# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Examples — every POSIX example binary is now named after its source file.
  - The rtc-client (prebuilt UDP lib) demos are renamed with a `udp_` prefix:
    `chat_demo` / `edu_camera_demo` become `udp_chat_demo` /
    `udp_edu_camera_demo` (sources renamed to match). The prefix is what
    disambiguates them from the rtc-tcp-client demos of the same name, which
    makes the `tai_` prefix on the TCP targets redundant — it was never part of
    their source-file names.
  - The rtc-tcp-client targets drop the prefix: `tai_text_chat_demo` →
    `text_chat_demo`, and likewise `tai_audio_chat_demo`, `tai_edu_camera_demo`,
    `tai_music_play_demo`, `tai_mcp_demo`, `tai_agent_trigger_demo` (sources
    unchanged; the `tai_` lives on in the `tai_*` API they call). Scripts and
    docs that invoke `./build/tai_*` must be updated.
  - Every other example already followed the rule (`activate_demo`,
    `dp_management_demo`, `ota_demo`, …) and is unchanged.

- **BREAKING** iot-client — MQTT auto-connect is on by default. The
  `mqtt_auto_connect` field is replaced by `mqtt_disable_auto_connect` on both
  `iot_client_config_t` and `iot_on_boarding_config_t`.
  - The rename is what makes the new default expressible: a zero-initialized
    struct gives a `bool` `false`, so a field named `mqtt_auto_connect` can only
    ever default to off. Reversing it follows `mqtt_disable_tls`, which already
    reads "false (default) = TLS on" in the same structs.
  - Existing code breaks at compile time rather than silently changing
    behaviour, which is the point of the rename: `.mqtt_auto_connect = false`
    becomes `.mqtt_disable_auto_connect = true`, and `.mqtt_auto_connect = true`
    can simply be deleted.
  - The failure path is deliberately unchanged: a failed auto-connect still
    releases the client and returns `NULL` from `iot_client_init()`. That now
    applies by default, so a device whose network may not be up at boot — or one
    that only uses ATOP over HTTP with no broker, like `examples/posix/ota-demo`
    — must set `mqtt_disable_auto_connect` and drive the link itself, or it will
    fail to initialize instead of coming up and retrying.
  - NOT covered by a test. Both existing auto-connect tests use an empty devid,
    so `mqtt_url` stays empty and the branch short-circuits before the flag is
    read — inverting the predicate leaves `iot_message_test` at 18/18. Proving
    the default needs a DNS mock and a broker mock in the same suite, which none
    of them currently has.

- **Docs — the cloud-VAD turn boundary is `TAI_EVT_CHAT_BREAK`, not
  `TAI_EVT_SERVER_VAD`.** The current Tuya AI cloud no longer sends
  `TAI_EVT_SERVER_VAD` (type 5) in cloud-VAD mode; it signals the end of a
  user turn with an inbound `TAI_EVT_CHAT_BREAK` (type 4) — the same event
  type used for "user spoke over the reply". The VAD guide, the
  rtc-tcp-client reference (event table, `tai_send_audio_end` warning), the
  FAQ and the module CONTEXT now teach the turn handling at
  `TAI_EVT_CHAT_BREAK`, and mark `TAI_EVT_SERVER_VAD` as legacy (constant
  kept for protocol compatibility; `tuya_ai.h` documents both).

### Added

- iot-client — `iot_client_get_session_token_ex()`, which reports *why* the
  cloud refused to issue an agent token.
  - The refusals a device actually meets in the field all arrive on this one
    path and need opposite handling: `GATEWAY_NOT_EXISTS` (removed from the
    cloud — re-provision), `CHILD_PRIVACY_AGREEMENT_REQUIRED` (agreement not
    signed yet — wait and retry, the user is expected to act in the app),
    `ISSUE_TOKEN_FAILED` (no agent configured for the product — retrying is
    pointless). `iot_client_get_session_token()` collapsed all three into
    `OPRT_ATOP_BUSINESS_ERROR`, so a device could only retry blindly.
  - `atop_base_response_t` already parsed `errorCode`/`errorMsg`, but
    `atop_ai_token_get()` dropped them on the floor. They now reach the caller
    through a new `iot_atop_rejection_t` out-parameter.
  - Additive: `iot_client_get_session_token()` keeps its signature and
    behaviour, and is now a wrapper passing `NULL`. Passing `NULL` for
    `rejection` is supported and means "don't care".

- iot-client — device-initiated reset (`iot_client_reset`), for a device that
  unbinds itself rather than waiting to be removed from the app.
  - New public `iot_client_reset()` in `iot_client.h` over a new
    `atop_device_reset()` wrapper for the cloud's `tuya.device.reset`. Until
    now every reset path in the SDK ran cloud → device (the protocol-11 notice
    behind `iot_reset_callback_t`); there was no way for the device to start it.
  - Success and failure decide who owns the client, and they differ: `OPRT_OK`
    means the cloud accepted the reset and the client has already been
    destroyed (everything `iot_client_deinit()` frees), so the pointer must not
    be used, freed or disconnected again. Any other code means nothing was torn
    down and the client is still fully usable, so the caller can retry. The
    return code answers "does the cloud know", not "is the client alive".
    Destroying on failure was rejected deliberately: a device that is locally
    unbound while the cloud still has it bound is worse than a retry.
  - Two non-responsibilities are documented at the declaration: it does not
    erase persisted credentials/DP state/schema (only the app knows where those
    live), and it does not wait for a protocol-11 notice — that push is what a
    *remote* removal looks like, while a device-initiated reset is acknowledged
    by the return code.
  - `iot_client_reset()` takes an optional `error_code` out-param, and it is not
    a convenience: `OPRT_ATOP_BUSINESS_ERROR` alone cannot separate
    `REMOTE_API_RUN_UNKNOW_FAILED` (server busy — retry) from a terminal
    `GATEWAY_NOT_EXISTS` (the binding is already gone — retrying never succeeds,
    wipe credentials and re-pair). Treating the second as retryable strands the
    device forever: it never wipes, never re-pairs.
  - Built on the existing generic entry (`iot_atop_call`) rather than a
    dedicated named wrapper. That entry already owns signing, AES-GCM, host
    resolution, envelope parsing and the pre-activation credential check — and,
    unlike a result-less wrapper, it surfaces the cloud's `errorCode`, which is
    exactly what the out-param needs. A first version duplicated all of it in an
    `atop_device_reset()`; removing that dropped ~80 lines and one of two
    parallel request-assembly paths.
  - Demonstrated in `examples/posix/pair/api-activate` under `--release`, beside
    the activation it undoes: binding and unbinding are the two ends of one
    lifecycle, and the ownership mirrors too — activation hands back a client, a
    successful reset destroys it. `unbind-demo` keeps the other mechanism, the
    cloud-initiated protocol-11 push, which has no return code to inspect and no
    local trigger at all.
  - New `iot_reset_test.c` (4 tests) pins the asymmetry, including that a
    rejected reset leaves the client retryable. It uses a heap client because
    the success path frees the struct; the mock answers a devId containing
    "busy" with the interface doc's own `REMOTE_API_RUN_UNKNOW_FAILED` so the
    failure path is reachable. Verified leak-free.
  - The interface version is `"5.0"`, not the `"1.0"` most `tuya.device.*`
    interfaces here use — easy to "correct" by mistake, and a wrong version
    fails only at the cloud, as a rejection that reads like a network fault. The
    mock therefore verifies `v=3.0` and answers anything else with
    `UNKNOWN_API_VERSION`, so a slip fails in `iot_reset_test` instead of on a
    real device.

- iot-client — MQTT connect/disconnect are now public API.
  - New `iot_client_connect()` / `iot_client_disconnect()` in `iot_client.h`,
    thin wrappers over the message layer in the same shape as the existing
    `iot_client_process()` / `iot_client_publish()` pair.
  - Publishing connect also exposed a latent leak, fixed here: connecting an
    already-connected client built a second link and orphaned the first.
    `iot_client_message_try_connect()` assigns `client->mqtt` unconditionally, so
    the previous mqtt client, its packet buffer and its open socket were left
    with nothing pointing at them. Reachable precisely because this API is now
    public and documented as *the* way to bring the link up: an app that also
    left `mqtt_auto_connect` true calls it on a live client. A second call is now
    success without rebuilding — disconnect first to force a fresh link — and the
    guard sits in the private entry point so every path is covered, including
    init's auto-connect. Pinned by `test_connect_twice_keeps_one_link`.
  - They close a real gap rather than adding a capability: `mqtt_auto_connect`
    defaults to `false`, and the only way to bring the link up was
    `iot_client_message_connect()` — declared in `src/iot_client_message.h`, a
    header that is not installed. `iot_client.h`'s own config comment told
    callers to invoke exactly that function, and four examples reached into the
    private header to get it, which only compiled because they build from inside
    the repo. An app built against the installed headers could neither connect
    manually nor re-establish a dropped link.
  - The default stays `false`. Flipping it would not have fixed this — a
    reconnect loop needs `connect()` regardless of the default — and would have
    changed behavior for every caller that zero-initializes its config,
    including devices that deliberately use ATOP over HTTP with no broker
    (`iot_client_init()` tears the client down and returns NULL when an
    auto-connect fails).
  - The four examples (`dp-management`, `ota-confirm`, `unbind-demo`,
    `ai/rtc-tcp-client`) now use the public API and no longer include the
    private header, so they compile the way an external consumer would.
  - Docs corrected while making the promise public: both headers claimed a TLS
    handshake failure "refreshes the MQTT CA certificate and retries once".
    No such code exists — `iot_client_message_try_connect()` destroys the client
    and returns `OPRT_TLS_HANDSHAKE_FAILED`. Cert recovery is the app's job
    (`iot_get_ca_certificate()` + reassign `client->cacert`); a reconnect loop
    written against the old wording retries the same doomed handshake forever
    after a broker cert rotation.

- common — coreMQTT's Error/Warn logs are routed into the SDK log facade
  (`common/core_mqtt_config.h`), so a broker's actual refusal reason is visible.
  - coreMQTT was built with `MQTT_DO_NOT_USE_CUSTOM_CONFIG`, leaving its `Log*`
    macros expanding to nothing. A refused CONNECT therefore reached the log as
    a bare `MQTT_Connect failed: 6` — `MQTTServerRefused` — while coreMQTT knew
    and discarded which of the five MQTT 3.1.1 reasons the broker sent.
    `logConnackResponse()` had the answer and logged it into a void.
  - The same connect now also prints `Connection refused: bad user name or
    password.` (or `identifier rejected` / `not authorized` / …), plus the
    SUBSCRIBE refusals that were equally invisible. Pinned by a new
    `test_connack_reason_is_logged`, which captures the log facade and asserts
    the broker's words arrive: the pre-existing `test_connect_auth_fail` drives
    the same refusal but asserts only the return code, so it passes with the
    routing disabled and cannot protect it.
  - Wired in both build paths. `examples/esp-idf/components/agentic_kit` sets
    its own compile definitions, so the ESP-IDF component kept the feature
    switched off — on precisely the embedded target the diagnostic exists for —
    until it was fixed too.
  - `core_mqtt` now declares its dependency on `agentic_kit_common`. Its objects
    reference `log_emit()`, and the link previously resolved only because
    another archive was scanned first and happened to pull `log.o` in.
  - Only Error and Warn are routed. `LogInfo`/`LogDebug` fire per packet, so
    wiring them would cost flash and bury the useful lines; they stay compiled
    out, which keeps this a diagnostics-only change with no behavioural or
    hot-path effect.
  - The SDK's own `mqtt.c` log lines that printed a bare `MQTTStatus_t` (all
    seven: Init, InitStatefulQoS, Connect, Subscribe, Publish and both
    ProcessLoop sites) now print the symbolic name alongside the number, via
    coreMQTT's own `MQTT_Status_strerror()` — `MQTT_Connect failed:
    MQTTServerRefused (6)`. The number is kept so existing logs and tickets
    still line up.
  - `docs-site/docs/reference/iot-client.md` gains a `MQTTStatus_t` table (for
    reading older logs, which carry only the number) and a CONNACK refusal-code
    table, noting that codes 4/5 usually mean the device was unbound in the
    cloud rather than a wrong password, and how to confirm that over ATOP. The
    sample output is the verbatim four-line block the SDK emits, timestamps
    included — a paraphrase there defeats the page's only purpose.

- common — coreHTTP's Error/Warn logs are routed into the log facade too
  (`common/core_http_config.h`), completing the pair. This is the half that
  matters when MQTT is already refusing: the docs send the reader to ATOP over
  HTTP to ask whether the credentials are still valid, and that path had no
  diagnostics of its own. An oversized response — the failure recorded below at
  contentLength 6558 — now says `insufficient space: responseBufferLen=...`
  instead of surfacing as a generic communication error.
  - The include directory is PUBLIC here where coreMQTT's is PRIVATE, and the
    asymmetry is real rather than an oversight: `core_http_client.h` includes
    `core_http_config_defaults.h` itself, so every consumer of the header needs
    the path, whereas only coreMQTT's own `.c` files do.
  - Pinned by `test_oversized_response_is_explained`, with a mock API that
    returns a deliberately over-sized envelope. Verified to bite: restoring
    `HTTP_DO_NOT_USE_CUSTOM_CONFIG` drops `iot_atop_call_test` to 13/14.

- iot-client — the three byte-identical cleanup blocks in `mqtt_client_connect()`
  (after `MQTT_Init`, `MQTT_InitStatefulQoS` and `MQTT_Connect`) are now one
  `mqtt_abort_connect()`. They were a standing hazard rather than just noise: a
  fix to the teardown sequence applied to one copy could silently miss the other
  two, and a nearby teardown fix had just landed on this branch.

- iot-client — `iot_client_deinit()` wipes the client before freeing it. `devid`,
  `secret_key` and `local_key` are plaintext arrays in `iot_client_t`, so on an
  embedded allocator the next comparable malloc handed the block — keys included
  — to unrelated code. Written through a volatile pointer, since a plain
  `memset()` immediately before `free()` is a dead store a compiler may elide.
  Matters most on the `iot_client_reset()` path, where the device is being
  decommissioned or handed to a new owner.

- Examples — `unbind-demo --reset` no longer requires a working MQTT connection.
  The reset call travels over ATOP HTTPS and needs no broker session, as the
  demo's own comment said, but the code gated it behind a successful
  `iot_client_connect()` — disabling the flag in the one case it exists for,
  since a device the cloud has already unbound has its CONNECT refused
  (CONNACK 4/5). The reset path now runs before any connect, and branches on the
  returned `errorCode` to show the terminal-vs-retryable distinction.

- iot-client — `iot_client_reset()` sends `resetFactory`, and the caller chooses
  it. The request body was `{"t":…}` only, which the cloud rejects: this
  interface requires the field.
  - New `iot_reset_scope_t` picks the value, and the two options differ in
    whether they can be undone. `IOT_RESET_UNBIND_ONLY` (`false`) gives up the
    user-device binding and leaves the device's cloud-side data in place, so
    re-pairing can pick it up again. `IOT_RESET_FACTORY` (`true`) additionally
    discards that data, business-specific exclusions aside, and **cannot be
    undone** — re-pairing yields a new binding, not the old state.
  - Same pair of meanings as the inbound protocol-11 classification, in the
    opposite direction, which is why it is a separate type: the existing
    `IOT_RESET_REMOTE_*` constants are named for a push the cloud initiated and
    read backwards on a call the device makes. An enum rather than a bool
    because a bare `true` at the call site would not say which one it is, and
    the wrong one is unrecoverable.
  - The mock requires the field and pins the choice: a devId containing
    "factory" must arrive with `resetFactory=true`, any other with `false`, so a
    scope that does not reach the wire is rejected rather than passing silently.
    Checked by inverting the mapping — `iot_reset_test` drops to 2/5. The
    success envelope still returns an empty `result`, as the real interface
    does.
  - `api-activate --release` uses `IOT_RESET_UNBIND_ONLY` so the demo can be run
    repeatedly; the comment there says what a real decommission would pass.

### Fixed

- Examples — `audio_chat_demo` died with `[parse_token] 'connect_conf' not
  found` on hosts whose `/opt/homebrew/include` holds mbedtls 4.x headers.
  That directory lands first on this one target's include path (via
  `find_path(opus)` for libopus), shadowing the vendored 3.6.6 headers, and
  mbedtls 4.x redefines `MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL` to a PSA
  status — so the NULL-buffer size probe in `b64_decode()` compared the
  vendored library's `-0x002A` return against `-138` and failed on every
  session token, which `parse_token()` then scanned as raw JSON. The buffer
  is now sized from the RFC 4648 4:3 ratio; the constant no longer
  participates. `text_chat_demo` and siblings never saw it because they
  carry no opus include path.

- Examples — `audio_chat_demo` now sends its one-shot file audio under the
  device-VAD contract it actually implements: `asr.enableVad` and
  `processing.interrupt` are `false` in the event user data, matching the
  `tai_send_audio_end()` the demo already sends after the file. The old
  attributes declared cloud VAD, under which that call is contractually
  wrong (it closes a turn the cloud owns) — a one-shot file turn is
  device-driven end to end (`docs/guides/vad-and-interrupt.md`).

- iot-client — a peer that closed a non-TLS MQTT connection went unnoticed until
  the 60 s keepalive expired. `pal.h` defines a 0 from `tcp_recv` as EOF, but
  coreMQTT reads a 0 from the transport as "no data yet", so `transport_recv()`
  had to translate it — which the TLS branch did, with a comment explaining
  exactly this, while the TCP branch (`mqtt_disable_tls = true`) passed it
  straight through. Pinned by `test_closed_peer_is_reported`.

- iot-client — tearing down an already-dead link no longer pushes a DISCONNECT
  packet into a socket that cannot carry it. Worth fixing once coreMQTT's errors
  became audible: the failed send is logged at ERROR level, so a device
  reconnecting on a flaky link reported two errors per cycle for an ordinary
  teardown. `mqtt_client_disconnect()` skips only the packet, still releasing the
  socket and buffer — clearing `connected` instead would have made the teardown
  return early and leak both.
  - Which statuses count is the whole difficulty. `MQTTBadResponse` (a malformed
    packet) and `MQTTIllegalState` (QoS bookkeeping) are protocol faults on a
    healthy socket; suppressing the DISCONNECT there would strand the session on
    the broker until the 60 s keepalive expired, and since the clientId is the
    devid, the next reconnect would race that stale session. Only
    `MQTTRecvFailed` / `MQTTSendFailed` / `MQTTKeepAliveTimeout` qualify, and a
    completed exchange clears the flag again — read at teardown, a latch-only
    flag would let one transient error the caller retried past suppress the
    DISCONNECT for the rest of a live session, reaching the same race from the
    other end.
  - All four coreMQTT call sites report their status: process, both subscribe
    sites, and publish. Publish matters most — every DP report goes through it,
    so it is the likeliest place an app discovers a dropped link.
  - Not covered by a test — a just-killed peer usually absorbs one more send, so
    the failing send does not reliably reproduce against a mock, and the
    flag-clearing path needs a transient failure followed by a success that the
    mocks cannot stage.

- iot-client tests — the log-capture handlers read past the end of their stack
  buffer. `vsnprintf` returns the length it *would* have written, so any routed
  line longer than the local buffer made `memcpy` copy beyond it, corrupting the
  capture with unrelated stack and able to fake or mask the very substring under
  assertion. One coreHTTP parse error interpolates up to a whole response buffer,
  so this was reachable. `test_connack_reason_is_logged` also asserted on the
  bare string `MQTTServerRefused`, which coreMQTT's own routed line already
  contains — it passed with `mqtt.c` reverted to a raw `%d`, protecting nothing.
  It now matches the SDK's own message.

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
