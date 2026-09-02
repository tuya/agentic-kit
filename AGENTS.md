# AGENTS.md

Instructions for AI coding agents working in this repository: a device-side C SDK for Tuya
hardware, spanning three bounded contexts — an IoT client (device ↔ cloud over MQTT/ATOP), an
RTC/TAI client (device ↔ AI Foundation, real-time audio over a custom binary framing), and BLE
provisioning — over a Platform Abstraction Layer, with mbedTLS / cJSON / coreMQTT / coreHTTP
vendored under `third_party/`. Sections follow the team outline.

## Commands

```bash
git submodule update --init --recursive          # mbedTLS nests a `framework` submodule
pip install jsonschema jinja2 pycryptodome cryptography
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=$(which python3)
cmake --build build -j 4
ctest --test-dir build --output-on-failure --no-tests=error --timeout 180   # never -j
```

- **One test**: `ctest --test-dir build -R iot_dp_test --output-on-failure`, or
  `./build/iot_dp_test` from any cwd — every mock path is an absolute compile-time define.
- **Sanitizers** — the only memory-error gate in CI:
  `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE=$(which python3) -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`
  then `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-asan --output-on-failure --no-tests=error --timeout 240`
- **Leaks**: `modules/iot-client/test/run_valgrind_check.sh ./build/tai_unit_tests` (Linux) or
  `run_leaks_check.sh ./build/iot_cipher_test` (macOS) — no-subprocess binaries only; under
  valgrind the Python mock handshakes time out. Both scripts **ignore the test's exit code**: a
  suite failing every assertion still prints "RESULT: NO MEMORY LEAKS DETECTED" and exits 0.
  Coverage is 3 of 13 binaries, hand-listed in `.gitlab-ci.yml`; GitHub's sanitizer job sets
  `detect_leaks=0`, so nothing else checks leaks anywhere.
- **Examples** — a separate top-level project, and `AGENTIC_KIT_BUILD_EXAMPLES` defaults OFF, so
  a plain root `cmake --build build` builds none:
  `cmake -S examples/posix -B build-examples -DPython3_EXECUTABLE=$(which python3) && cmake --build build-examples -j 4`
  Three demos then remove themselves on a `message(STATUS)` alone — `chat_demo` /
  `edu_camera_demo` without a matching prebuilt `libstm.a`, `tai_audio_chat_demo` without
  libopus — so confirm the specific target actually got built.
- **Docs site**: `cd docs-site && npm ci && npm run build` (node ≥ 20).
- **Version**: `tools/bump_version show | next [--major|--minor|--patch] | release` — README's
  `tools/bump_version.sh` and `tools/bump_version_test.sh` do not exist; the script has no
  extension and no separate self-test.

## Architecture

- **Two threading models, and the app owns only one loop.** iot-client: single-threaded,
  app-driven — nothing runs unless the app calls `iot_client_process()`, and every DP / message /
  reset callback fires on that thread. rtc-tcp-client: `tai_connect()` spawns a worker via
  `pal->thread_create`, and every receive callback fires there, concurrently with the app.
  tuya-ble: no thread — it runs on whatever context the port's BLE stack calls `tuya_ble_recv()`
  from. Never move work between these worlds.
- **The SDK does no background work.** It never auto-reconnects MQTT, and the DP layer never
  publishes on its own — no report-on-connect, no cloud-query-triggered report
  (`modules/iot-client/docs/adr/0001-dp-layer-never-initiates-uplink.md`). The app owns the
  connect/reconnect loop and every uplink.
- **`modules/rtc-client` is a prebuilt closed-source library**: four headers plus one `libstm.a`
  per architecture — no source, no CMakeLists, no test. Call its API, never change it; no change
  to it is verifiable offline. The root CMakeLists declares `tuya_steam_client` only when the
  current platform's `libstm.a` exists (its if/elseif chain covers three of the five
  architectures on disk).
- **coreMQTT and coreHTTP route their Error/Warn logs into the log facade** via
  `common/core_mqtt_config.h` and `common/core_http_config.h`, and must keep doing so. Setting
  `MQTT_DO_NOT_USE_CUSTOM_CONFIG` / `HTTP_DO_NOT_USE_CUSTOM_CONFIG` again silently discards each
  library's own account of a failure: a rejected CONNECT decays to a bare `MQTTServerRefused` with
  no `Connection refused: bad user name or password.`, and an oversized response to a generic
  communication error with no `insufficient space`. Both build paths must be kept in step — the
  root `CMakeLists.txt` **and** `examples/esp-idf/components/agentic_kit`, which carries its own
  `target_compile_definitions` and was for a while the path where none of this reached a device.
  Pinned by `test_connack_reason_is_logged` and `test_oversized_response_is_explained`.
  `LogInfo`/`LogDebug` stay compiled out on purpose (per-packet, costly on flash), and coreMQTT's
  thread-safety hooks are still unset.

## Conventions

1. **Domain language is not optional.** `CONTEXT-MAP.md` points at one `CONTEXT.md` per context;
   each defines the terms *and* the words to avoid. Use those exact words in code, commit
   messages and docs, and add new vocabulary to that `CONTEXT.md`. Architectural decisions go in
   `modules/<mod>/docs/adr/`.
2. **A feature commit touches four things, not one.** Conventional Commits with a module scope
   (`feat(iot-client):`, `fix(ota):`, `docs:`; `!` marks a breaking change) and a body explaining
   *why* that ends with the test counts it was verified against. Anything that changes what SDK
   users see also needs a `## [Unreleased]` entry in `CHANGELOG.md` and, when it is worth
   documenting, a docs-site page plus its id in `sidebars.ts`. Repo-internal work does not:
   `chore:` and `fix(build):` commits here carry no CHANGELOG entry, and neither does this file.
   Agent-authored commits end with a `Co-Authored-By:` trailer.
3. **`modules/*/include/` is the public API; `src/` is not** — but the host build puts
   iot-client's `src/` on the *PUBLIC* include path, so app code that includes a private header
   compiles here and fails on ESP-IDF (two shipped demos do this). To expose something, promote
   the declaration into `iot_client.h` with `IOT_API`.
4. **Every new `.c` goes in two source lists**: the root `CMakeLists.txt` and `AK_SRCS` in
   `examples/esp-idf/components/agentic_kit/CMakeLists.txt`. No CI job runs `idf.py`, so an
   omission is invisible until someone flashes a board. They diverge on purpose — `pal_posix.c`
   and `iot_pal_defaults.c` host-only (each IDF app defines its own `get_default_pal()`),
   `pal_freertos.c` IDF-only — so never blind-sync them.
5. **Module code goes through the PAL**: `pal->malloc`/`pal->free` for memory, `log_emit` for
   output (via each module's prefixed `log_info`/`log_warn`/`log_error`). A direct `malloc` or
   `printf` is a porting bug even where it links on the host. The one deliberate gap: `pal_t` has
   only a monotonic `time_ms`, so ATOP signing reads libc `time(NULL)` — a port needs a real-time
   clock the C library can see, or every signed request carries a `t` the cloud rejects.
6. **CHANGELOG entries are terse, and carry a PR number.** One line per change —
   `- <module> — <what changed>(#<PR>).` — with indented sub-bullets only for specifics a reader
   acts on (a new symbol, a changed default, a migration step). `## [0.3.0]` is the reference for
   length and shape; put the entry under the right `Added` / `Changed` / `Fixed` heading, since
   release notes are generated from those. Motivation, rejected alternatives, verification detail
   and anything else answering *why* belong in the commit body — that is where a reader who wants
   them will look, and repeating them here produces release notes nobody finishes reading. The PR
   number is what links the one line back to all of it, so an entry without one is incomplete.

## Invariants

<!-- properties that fail SILENTLY: breaking one leaves every check green -->

Scoped to work that happens routinely. An invariant that only fires on a rare action is not
written here but at its edit site, because whoever finally does that work has the least context
and will have that file open: PAL porting in `pal/pal.h` and `pal/pal_freertos.c`, BLE
provisioning in `modules/tuya-ble/CONTEXT.md`, frame signing in
`modules/rtc-tcp-client/CONTEXT.md`, the `OPRT_*` allocation rule in
`modules/iot-client/include/iot_dp.h`, the 4096-byte ATOP response ceiling in
`docs-site/docs/guides/atop-generic-call.md`, and the test harness's virtual clock in
`modules/rtc-tcp-client/test/tai_pal_loopback.h`. Move one back here only if it starts firing on
ordinary changes.

### Build and tests

- **An unregistered test compiles silently and the suite still reports 100%.** Registration is by
  hand, twice: call it from the suite's `main()` (`RUN_TEST(fn)` in the iot suites, a bare call in
  the tai suites, `EXPECT_TRUE(fn() == 0)` in `tuya_ble_test`) *and* wire it into
  `CMakeLists.txt` — `agentic_kit_add_iot_test(name source)` for an iot-client suite,
  `add_executable` + `add_test` otherwise; a binary that spawns no subprocess also goes in
  `.gitlab-ci.yml`'s valgrind list. Each suite derives its count from the calls it actually made,
  and no target in this project enables `-Wall`, so an uncalled `static` test is not even a
  warning. Never delete `CMakeLists.txt`'s explicit `enable_testing()` (the comment above it says
  why): re-configures would silently stop regenerating the ctest list, `ctest` exits **0** on an
  empty list, and GitLab's test stage has no `--no-tests=error`.

### iot-client — DP state and schema

- **`schema` and `dp_state` are one artefact; persist and restore them together.** A NULL, empty,
  `"[]"` or unparseable schema is not an error anywhere: `iot_dp_rebuild()` installs *loose mode*
  with one `log_info` line and `iot_client_init()` still returns a healthy client. In loose mode
  cloud DP-sets are never dispatched, every `iot_dp_get`/`set` returns `OPRT_DP_INVALID_ID`,
  `iot_dp_restore_json()` discards the whole snapshot and still returns `OPRT_OK`, and
  `iot_dp_dump_json()` returns `{"dps":{}}` — which an app that periodically persists writes over
  its own good snapshot, making the loss permanent.
- **Dirty bits are never persisted, and two paths silently clear them.** (1) Restore marks nothing
  dirty, so `iot_dp_report_all_dirty()` after a boot is a guaranteed no-op returning `OPRT_OK` and
  the cloud serves its pre-reboot values forever — first activation works because
  `iot_dp_init_defaults()` dirties everything, so this appears only on a device's *second* boot.
  (2) `iot_dp_schema_check_update()` snapshots, rebuilds, restores with `mark_dirty=false` and
  re-defaults, so a DP the app had `iot_dp_set()` but not yet reported comes back clean — and
  `dp_fire_save` appears nowhere in that path. So: call `iot_dp_report_all()` (not `_dirty`) after
  every successful (re)connect, and in `iot_schema_update_callback_t` persist the new schema
  **and** re-dump the DP state before reporting. Otherwise the two persisted artefacts describe
  different schemas, and next boot the `iot_dp_validate_json()` gate — which breaks on the *first*
  non-conforming entry — discards the entire DP state. `examples/posix/dp-management/` has this
  bug.
- **Schema constraints are read only from the entry's `property` object** — `range` (enum only),
  `min`, `max`, `maxlen`. An enum DP without `property.range` loses its upper-bound check *and*
  switches wire form from the label string to a bare integer (`"4":2`, not `"4":"charging"`),
  which the cloud ignores; top-level `min`/`max`/`maxlen` on a `value`/`string` DP means no range
  check at all. Publishing succeeds either way and dump/restore stays self-consistent, so
  persistence tests pass too.
- **Access mode is never parsed, let alone enforced**, though `CONTEXT.md` states the ro/wr/rw
  rule as a domain fact (`mode` appears in `src/` only in a comment). The parser reads `id`,
  `type`, `min`, `max`, `maxlen`, `range` and nothing else; `iot_dp_set`/`iot_dp_report` on a `wr`
  DP return `OPRT_OK` and publish, and any rejection is cloud-side with no device-visible signal.
  Enforce direction in the application if the product needs it.
- **A protocol-5 downlink is always "consumed", even when nothing was applied.**
  `iot_dp_dispatch_downlink()` returns consumed for every protocol-5 envelope — non-object `dps`,
  every DP rejected, or the callback snapshot's malloc failing — with per-DP rejections at
  `log_warn` only. No DP callback, no `message_callback`, no error return, and the cloud already
  has its QoS1 ack. If the app needs to see rejected DP-sets, add an explicit path.

### iot-client — credentials, config, transport

- **A new `iot_client_config_t` field must be written in three places.** Both on-boarding entry
  points build a fresh `iot_client_config_t client_config = {0}` and hand-copy ~14 fields before
  delegating to `iot_client_init()`. A field wired only in `iot_client_init()` reads 0/NULL/false
  on every device that *activates*, while the restart path (init with persisted credentials —
  what every test and manual re-run takes) works perfectly.
- **`secret_key` authenticates, `local_key` decrypts.** MQTT's password is an MD5 of
  `secret_key`; every payload is AES-GCM'd with the first 16 bytes of `local_key`. A wrong or
  truncated `local_key` leaves a fully connected, completely deaf client: CONNECT, SUBACK and
  every ATOP/OTA/session-token call succeed, `iot_client_process()` and `iot_client_publish()`
  keep returning `OPRT_OK`, and every inbound DP-set and device-remove notice is dropped. Persist
  devid/secret_key/local_key together, and verify a round-trip decrypt — not just that CONNECT
  succeeded.
- **Everything that reaches the MQTT client must run on one thread.** `struct mqtt_client` has no
  mutex, coreMQTT's thread-safety hooks are compiled out, and `MQTT_Publish` sends one PUBLISH as
  several transport writes. The DP layer's mutex covers DP state only and is deliberately released
  before publishing. Do not add a reporter thread beside the process loop: every ctest binary is
  single-threaded, so no sanitizer can see the splice.
- **A new `iot_region_t` needs a `case` in BOTH env arms of `iot_region_to_host()`**, plus an
  entry in `iot_region_to_string()` (which returns the *wire* code, not the enum name). Both
  switches end in `default:`, so `-Wswitch` never fires and a miss succeeds at the HTTP layer
  against a real, wrong data center; `dns_test.c` asserts only the PROD arm. Measured facts no
  file in the repo records (probed 2026-08-14): `SG` is absent from the PRE arm because it does
  not exist in pre; `IOT_UEAZ_HOST` shipped as `a1-ueaz.tuyaeu.com`, which is **NXDOMAIN** — UE
  lives under `tuyaus.com`, so the UE ATOP fallback was dead until it was repointed at
  `a1-ueaz.tuyaus.com` (2026-08-31); and on prod West-Europe's `httpsUrl` *flaps*
  rather than being absent, so the device alternates between the DNS answer and the compile-time
  fallback across reboots.
- **`OPRT_OK` from `atop_base_request()` means `success == true`, nothing more.**
  `response->result` may be NULL, a JSON `null`, a bare string or an array, and
  `cJSON_GetObjectItem(NULL, ...)` returns NULL rather than faulting — so a wrapper that parses
  straight through fills its struct with zeros and returns `OPRT_OK`, indistinguishable from a
  genuine empty answer. The in-tree wrappers deliberately split both ways; a new one must decide
  explicitly what an absent `result` means for its interface. Always declare
  `atop_base_response_t r = {0};` and re-zero before a retry: twelve early returns fire before
  the envelope is parsed, and the success path never touches `error_code`/`error_msg`.
- **cJSON's allocator is the PAL's, bound process-wide exactly once in `iot_init()`.** Several
  paths free a `cJSON_Print*` buffer with `pal->free` and vice versa, correct only because of that
  binding; both sides are `void (*)(void *)`, so a mismatch is a free against the wrong heap
  rather than a type error — invisible in CI because every test PAL is plain `malloc`/`free`.
  Never introduce a second PAL, a per-client allocator, or a path that can return parsed JSON
  before `iot_init()`.
- **OTA verification fails open, in two ways.** `iot_ota.c` never calls `iot_ota_verify_*` — the
  app drives init/update/finish itself, and the only `iot_ota_verify_init()` return that may be
  treated as "skip" is `OPRT_NOT_SUPPORTED` (`iot_ota_verify_finish(NULL)` is a parameter error,
  not a pass). And `has_upgrade` is derived from the download URL alone while the cloud sends `""`
  for a digest it has not configured, so a response with no `md5` and no `hmac` silently
  downgrades the fleet to no verification — the shipped demo flashes anyway. Decide that policy
  explicitly: a good image upgrades normally either way, so the hole is invisible until someone
  substitutes the payload.

### rtc-tcp-client (TAI)

- **Receive-callback payloads are slices *inside* the caller's `tai_ctx_t`** —
  `tai_text_msg_t.text` and `tai_event_msg_t.data` point into the inline `ctx->rx_buf` /
  `ctx->frag_buf`, often a static array with no redzone. `tuya_ai.h` already says borrowed and not
  NUL-terminated; what it cannot say is that a `strlen`/`sscanf`/`cJSON_Parse` over-read therefore
  stays *in-bounds* for ASan and silently reads whatever the previous frame left. Copy at most
  `msg->len` bytes out first, or use a length-bounded scanner.
- **A new `TAI_EVT_*` must be added to `is_known_event()` too** (and `tai_event_type_name` for
  logs). Unknown event types are deliberately tolerated — WARN, skip, `TAI_OK`, link left up — so
  the feature simply never reaches `on_event`, and nothing correlates the two hand-maintained
  lists.
- **A server `SESSION_CLOSE` leaves `ctx->connected = 1`** — a non-terminal disconnect that does
  not latch the guard (see CONTEXT.md). Every `tai_send_*` guards on `connected` alone, so sends
  keep succeeding and returning `TAI_OK` into a session the server has closed. The app must stop
  sending and reconnect on its own; the SDK will not tell it twice.
- **`on_audio` fires N times per downstream AUDIO packet, all with the same `stream_flag` and
  timestamp** (the packet is split into `rx_audio_frame_size`-byte Opus frames, each delivered
  separately). An app that flushes its decoder or jitter buffer whenever
  `stream_flag == TAI_STREAM_START` throws away N−1 of every turn's first frames; the audio just
  sounds clipped, with no error anywhere.
- **Audio turn lifecycle — every wrong call still returns `TAI_OK`.** Under server VAD, the
  default (the built-in `chatAttributes` set `asr.enableVad: true`, so the cloud owns
  segmentation), never call `tai_send_audio_end()`, not even on the turn-boundary event: it
  sends Audio END + EventPayloadsEnd + EventEnd and closes the turn, cutting the user off
  mid-sentence. The current cloud signals that boundary with an inbound `TAI_EVT_CHAT_BREAK`
  and no longer sends `TAI_EVT_SERVER_VAD` — handle the break there (clear the interrupted
  turn's downlink; the uplink stays open). Only device-side VAD / push-to-talk ends a stream
  per utterance. And `tai_send_audio_end()` does
  not reset `audio_started`, so a `tai_send_audio_chunk()` after an `_end` without a fresh
  `_start` goes out with stream flag MIDDLE and no `audio-params` attribute — accepted by the
  transport, and ASR produces nothing. Begin every utterance with `tai_send_audio_start()`.

## Notes

- **`OPRT_DP_INVALID_ID` (-9) for a DP that *is* in your schema** means the registry does not
  contain it: the schema failed to parse (loose mode), or neither `property.type` nor the
  top-level `type` names one of `bool`/`value`/`string`/`enum`/`raw` — a Tuya `bitmap` DP, or an
  entry left with only the transfer category `"obj"`, is dropped by a bare `continue` **with no
  log at all**. Diagnose from the `dp: registry rebuilt (N DPs, schema|loose)` line and compare N
  with your schema's length. DP-state keys also go through `atoi()`, so a snapshot keyed by DP
  codes (`"switch_1"`) maps to DP 0 — gate every restore on `iot_dp_validate_json()`.
- **`ctest -j` is unsupported.** Mock ports are compile-time constants shared across suites
  (five bind 8443, plus 8198 and 11885) and `add_test` declares no `RESOURCE_LOCK`, so parallel
  runs collide — sometimes loudly, sometimes by passing against the wrong server. After a
  crashed run: `pkill -f atop_mock.py`.
- **"ATOP mock (8443) never became connectable"** is not a port problem: the fork+exec'd Python
  child died or never bound, and the parent only polls the port — it never `waitpid`s, so the real
  error is never printed. Usually the interpreter (`PYTHON3_EXEC` is an absolute path baked at
  configure time, so a Homebrew or venv upgrade breaks every mock-driven test — re-run cmake) or a
  missing `pycryptodome` in it. Reproduce by hand:
  `python3 modules/iot-client/test/mock/atop_mock.py`.
- **A mock that answers plain HTTP looks like a broken cert.** Three mocks gate TLS on
  differently-spelled env vars — `ATOP_MOCK_USE_SSL`, `DNS_MOCK_USE_SSL`, `MQTT_MOCK_USE_TLS` —
  and a fork+exec must `setenv` it in the *child*. Without it the mock serves plain HTTP, the
  readiness probe still succeeds, and the SDK reports `OPRT_TLS_HANDSHAKE_FAILED`, which reads as
  a CA bug in the SDK.
- **`tai_unit_tests` prints "OK" per case unconditionally** — `PASS()` is a bare `printf` after
  the checks, and `CHECK` prints FAIL and bumps a counter without returning, so execution
  continues into code that assumed the condition. Read the final
  `=== Results: N passed, M failed ===` line, or the exit code.
- **Pushing to a `feat/...` branch runs zero GitHub CI.** The push trigger matches `feature/**`,
  and every branch here is named `feat/...`; the first run happens when you open the PR against
  master. "No red X" on a feature branch means nothing was checked.
- **Docs changes are checked in only one place.** `docs-site` is `onBrokenLinks: 'throw'`, so
  renaming or deleting a page breaks `npm run build` for every page linking to it — but
  `paths-ignore` excludes docs-only pushes from all C jobs, and `deploy-docs.yml` triggers on
  `main`, which does not exist here (only its `workflow_dispatch` fires it). GitLab's `pages` job
  is the only thing that actually catches it, so build the site locally after touching it.
- **`-DLOG_LEVEL=...` does nothing.** `common/log.h` advertises it as a compile-time ceiling but
  no file references it; `log_emit()` filters on a runtime global, after the varargs have been
  evaluated. The only working compile-time gate is `TAI_LOG_LEVEL`, and it covers rtc-tcp-client
  only.
- **`mqtt_tls_config_t.verify_peer` is dead** — assigned in one place, read nowhere. Peer
  verification is decided solely by whether `cacert` or `cert_bundle_attach` is non-NULL;
  leaving both NULL is not "use the system trust store" (there is none on an embedded target),
  it connects with verification disabled behind one `log_warn`.
- **`iot_client_process(client, timeout_ms)` ignores `timeout_ms`.** The real blocking budget is
  the compile-time `MQTT_RECV_TIMEOUT_MS` (1000 ms), and the CONNECT sets a 60 s keepalive. Do
  not use the argument to pace the app loop.
- **`iot_client_connect()` never refreshes the CA and never retries.** The app owns cert
  recovery: on `OPRT_TLS_HANDSHAKE_FAILED`, call `iot_get_ca_certificate()` and reassign
  `client->cacert` yourself before reconnecting (see `examples/posix/dp-management/`).
  A reconnect loop that skips this retries the same doomed handshake forever after a cloud
  cert rotation. Both headers claimed the opposite until the API was made public; if that
  "refreshes the CA and retries once" wording ever reappears, it is fiction — no such code
  has ever existed in `iot_client_message_try_connect()`.
