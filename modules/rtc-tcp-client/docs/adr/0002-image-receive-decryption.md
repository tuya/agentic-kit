# Fixed image key and TuyaOpen encrypted-media Connections

Status: accepted by explicit user authorization; implemented in production code.
Keyed Connections, all three image-upload encryption paths and the RAW/Base64/URL
receive pipeline are enabled and verified against a local TLS peer.
Live-cloud compatibility remains unverified.

## Decision

The application explicitly acquires the fixed image key using `iot_atop_call()`
with `thing.km.secret.get.device` version `1.0`, or reuses a key it already owns.
It validates `result.secretValue` as Base64 decoding to exactly 16 bytes and
supplies the binary bytes through `tai_config_t.image_key`.

The constructor copies the key after fallible initialization. Presence is determined
by the pointer, so an all-zero key is valid. The owned immutable bytes are separate
from Frame encryption/signing keys, survive disconnect and failed Connection
attempts, and are wiped with `mbedtls_platform_zeroize()` on context destruction.
The owner must stop/join the worker before destruction. There is no SDK acquisition,
cache, refresh, persistence, setter, or IoT initialization/onboarding change.

The same `TAI_IMAGE_KEY_LEN` (16)-byte image key is used for uplink encryption and
downlink decryption. This supersedes the original receive-only scope and the
former decryption-only naming; no separate upload key or runtime setter is added.

The user explicitly authorized alignment with TuyaOpen's encrypted-media settings,
superseding the previous signing/image-only-suite blockers. A supplied key selects
Frame version 3 (reserved bits 2), no Frame signature (sign level 0, ignoring
`cfg.sign_level`), v2 Attribute layout and the exact 36-byte ClientHello suite
`00 || random[32] || 04 03 03`. The suffix means AES-128-GCM, whole video I-frames
and whole images as encryption ranges, not media capabilities. We do not invent
an image-only suite or advertise video/file Data IDs; video/file handling is not
implemented. Public `protocol_version` stays 0 or `TAI_VER_21`, not 3.

Keyed SessionNew advertises send IDs 1 (audio), 3 (text), 5 (image), and receive
IDs 2 (audio), 4 (text), 6 (image). TuyaOpen's sequential even receive-ID allocator
supports this allocation by configured type order; it is not evidence of a universal
fixed mapping from media type to ID. Keyed image receive requires ID 6.

**Security decision:** image AES-128-GCM uses no AAD. Its per-Packet tag authenticates
the encrypted image body, not the media metadata, Attributes or control traffic.
Unsigned Frames therefore require verified TLS to protect that metadata/control
traffic, URL pass-through and empty markers. Configure a nonempty `cfg.ca_cert`
(full PEM or bare Base64 body, not a file path; borrowed for context lifetime) or
`cert_bundle_attach`. `tai_connect()` rejects `disable_tls` or neither trust source
with `TAI_ERR_TLS` before networking. TLS uses required peer verification; a trust
pointer is not proof of a valid certificate or successful handshake. There is no
raw-TCP escape hatch or legacy-profile fallback for a keyed context.

A NULL key keeps legacy Frame version 2, signing defaults, 33-byte suite, Session
payload and image bytes unchanged. The optional CA field can also enable required
peer verification without a key; omitting it retains the legacy TLS configuration.
Rebuild SDK and consumers for the public config and typed image-message layouts.
Implemented source alignment is not evidence of live-cloud interoperability.

## Implemented processing

- `tai_send_image()`, `tai_send_image_with_text()` and
  `tai_send_image_audio_start()` prepare encrypted image data before EventStart.
  Nonempty input uses `len + 32` bytes of scratch: a 16-byte IV, ciphertext and a
  16-byte tag. Empty input uses 16 bytes (IV only). The caller's input is not
  modified; temporary scratch is wiped and freed on success and failure.
- Receive processing runs after complete-Packet transport reassembly. The extended
  header is eight media bytes plus a 16-byte IV. RAW contains ciphertext and a
  trailing 16-byte tag; canonical Base64 is decoded first. AES-128-GCM uses no AAD,
  separate bounded plaintext storage and authentication before delivery. Tag-only
  bodies authenticate too; header-only START/ONE_SHOT/END are zero-length markers,
  not authenticated image data, and header-only MIDDLE is rejected.
- URL bytes are passed through as `TAI_IMG_PAYLOAD_URL`, never fetched or decrypted.
  Decrypted bytes are typed `TAI_IMG_PAYLOAD_RAW` in `tai_image_msg_t.payload_type`.
  Without a key, legacy bytes remain unchanged and the parsed Packet type is
  reported (RAW when absent). No JPEG/PNG-to-pixels decoding is provided.
- Keyed parsing validates exact bounded image-params fields and ranges, duplicate
  image-params and unambiguous Event IDs. START/ONE_SHOT supplies parameters;
  MIDDLE/END must match the active Event/Data ID and any repeated parameters.
  Missing continuation Attributes inherit payload type, not format/dimensions.
  Completion, Event boundaries, SessionClose, disconnect and failures reset stream
  state. No failed Packet is delivered and no key request or plaintext fallback
  follows an error.
- Dispatch returns positive fatal details: `TAI_PROTO_ERR_IMAGE_AUTH = 11` for
  GCM authentication failure and `TAI_PROTO_ERR_IMAGE_PROCESSING = 12` for scratch
  allocation/processing failure. Existing malformed-header/Packet and unexpected
  state details remain in use. The worker reports one terminal
  `on_disconnect(PROTOCOL, detail)`; the application discards any incomplete image.
- Callback delivery remains on the worker. All pointers are borrowed until callback
  return; plaintext can occupy temporary PAL heap storage and is wiped/freed then.
  Retaining image bytes requires an application copy, not retaining the pointer.
  URL bytes remain length-delimited, not necessarily NUL-terminated.

## TuyaOpen source evidence

Reference: `/Users/wangsn/work/device-dev/TuyaOpen/image-decryption.md`, checked
against the source paths below in that checkout. These observations describe
`AI_VERSION == 2` / `AI_SUB_VERSION == 2`, not a captured target-cloud exchange.

| Concern | Exact source evidence | Implemented decision |
| --- | --- | --- |
| Suite bytes | `src/tuya_ai_service/svc_ai_basic/src/tuya_ai_protocol.c:1368` builds 36 bytes; line 1369 writes `AI_SIGN_LEVEL_0`, line 1371 copies 32 random bytes, and lines 1373-1383 append the nonzero data/video/image settings. `src/tuya_ai_service/svc_ai_agent/src/tuya_ai_agent.c:1252` sets `AI_DATA_SL4`, `AI_VIDEO_ENCRYPT_I_FR_ALL`, `AI_IMAGE_ENCRYPT_ALL`. | Send exactly `00 \|\| random[32] \|\| 04 03 03`, not a guessed image-only variant. |
| Suite meaning | `src/tuya_ai_service/svc_ai_basic/include/tuya_ai_protocol.h:276` defines `AI_DATA_SL4` as AES128-GCM; line 282 defines whole video I-frame encryption as 3; line 288 defines whole-image encryption as 3. | Encryption settings are not a media-support bitmap. No video/file support is claimed. |
| Frame/signing | `src/tuya_ai_service/svc_ai_basic/src/tuya_ai_protocol.c:950` selects version 3 for sub-version 2; line 955 fixes reserved bits to 2. Lines 981-1001 append signatures only in the version-1 branch, not version 2. | User-approved unsigned version-3 Frames; ignore `cfg.sign_level`, do not use an invalid sign level as a workaround. |
| Attribute layout | `src/tuya_ai_service/svc_ai_basic/src/tuya_ai_protocol.c:697` writes the 32-bit Attribute block length; lines 701-718 pack 16-bit type/length in the non-v1 branch, and lines 835-865 unpack that layout independently of the Frame sub-version. | Retain v2 layout (32-bit block length, 16-bit type/length). |
| Data IDs | `src/tuya_ai_service/svc_ai_agent/src/tuya_ai_agent.c:985` assigns receive IDs in configured type order. `src/tuya_ai_service/svc_ai_basic/src/tuya_ai_biz.c:1467` allocates odd send IDs and line 1478 allocates even receive IDs starting at 2. Lines 1374-1400 serialize SessionNew lists of IDs. | SDK audio/text/image order uses send 1/3/5 and receive 2/4/6; these are Session allocations, not universal type numbers. |
| Image body | `src/tuya_ai_service/svc_ai_basic/include/tuya_ai_protocol.h:532` defines the image header with a 16-byte IV for sub-version 2. `src/tuya_ai_service/svc_ai_agent/src/tuya_ai_agent.c:251` decodes Base64 per callback; line 355 decrypts with the final 16-byte tag and no AAD. | Authenticate each complete Image Packet after transport reassembly. Do not copy the reference's `eff_len > 16` authentication bypass; tag-only bodies authenticate and shorter nonempty bodies fail. |
| Uplink/empty markers | `src/tuya_ai_service/svc_ai_basic/src/tuya_ai_biz.c:274` allocates tag space only for a nonempty payload; line 313 uses image IV, fixed key and no AAD, then line 323 appends the tag. | All three send APIs encrypt nonempty images; empty images retain the extended header/IV without a tag. |

Local implementation mapping: `include/tuya_ai.h` defines key/CA ownership and the
public contract; `src/tai_client.c` selects the immutable profile, enforces TLS,
emits version-3 Frames in both send paths and checks the receive version;
`src/tai_protocol.c` emits the exact suite and Session lists, validates any returned
suite settings and processes images on ID 6. Paths are relative to this module.

The reference secret fetch passes no business parameters. Documentation uses `{}`
and the local ATOP mock verifies that shape, but cloud acceptance is not yet
verified. Generic ATOP remains untyped: malformed business results can return
`OPRT_OK` and must be rejected by the application. Only this exact secret API's
response-body diagnostics are suppressed, request-locally, including plaintext
Envelope fallback; unrelated API diagnostics remain intact.

## Remaining validation

The source mapping and user-approved security decision resolve the former
Connection blockers; the production path is enabled. Real verified-TLS tests use
public construction/connect with an in-process mbedTLS peer and existing synthetic
certificate fixtures. They cover CA/bundle trust, wrong CA/hostname rejection,
pre-network raw/missing-trust rejection, exact ClientHello/Session bytes,
wrong-profile rejection, worker image callbacks, all upload APIs, ChatBreak with
same/omitted Event ID, and reconnect with the same immutable key.

Fresh validation on 2026-10-14:
- Full serial host CTest: 20/20 passed.
- Full serial ASan/UBSan CTest: 20/20 passed with `detect_leaks=0` and
  `halt_on_error=1`; not a leak-detection result.
- Docs typecheck, locale parity, link tests and both locale builds passed.
- ESP32-S3 `idf -B build-image-decryption build` compiled and linked; existing
  certificate-bundle callback cast warnings remain. No flashing was performed.

An authorized live-cloud exchange remains pending for acquisition with `{}`,
ClientHello/acknowledgement/Session acceptance, image boundaries and empty markers
in both directions, and same-key reconnect. Source alignment and offline fixtures
must not be reported as successful cloud verification.

Keep the 32,000-byte reassembled-Packet default until representative cloud sizes and
target peak memory are measured. RAW receive needs bounded separate plaintext
scratch, while Base64 also needs a decode buffer; upload scratch is described above.
No target-cloud test or runtime heap/stack measurement is recorded here. No new
runtime dependencies are required.

## Historical gated-pipeline validation (2026-10-14)

These recorded results predate enabled keyed Connections and required verified TLS.
They are historical only, not validation of the current production profile or the
present documentation edits. Fresh results are recorded above.

- Serial host CTest in `build-feature`: 20/20 passed.
- Serial ASan/UBSan CTest in `build-asan-feature`: 20/20 passed with
  `detect_leaks=0` and `halt_on_error=1`. Scratch allocation/free balance is checked
  by test PALs; this is not a general leak-detection result.
- Worker parser tests cover fragmented RAW/Base64 delivery, authentication failure,
  allocation failure, strict wire Attribute validation, exact Packet ceilings and
  lifecycle reset. They install the key in an admission-paused legacy loopback
  context; they do not prove encrypted-profile handshake compatibility.
- Docs typecheck, locale parity, link tests and both production locale builds passed.
- ESP32-S3 consumer `examples/esp-idf/ai/rtc-tcp-client` compiled and linked with
  `idf -B build-image-decryption build`. Existing cert-bundle function-pointer cast
  warnings remain. No flashing, cloud exchange or runtime memory measurement.

## Historical preparatory validation

The following results describe the earlier key-ownership/crypto-primitive revision,
not the current upload and receive pipeline. They must not be treated as validation
of the current changes or as evidence of cloud compatibility.

- Serial host CTest: 20/20 passed, including both log-disabled TAI variants,
  packet-log compile matrix, legacy receive integration and secret-API logging.
- Serial ASan/UBSan CTest: 20/20 passed with `detect_leaks=0` and
  `halt_on_error=1`. This is not a leak-detection result. UBSan found an
  intermediate out-of-bounds pointer in the new truncation test; parenthesizing
  the offset fixed it before the successful full rerun.
- `npm run build` in `docs-site`: typecheck, locale parity, link checks and both
  locale production builds passed.
- ESP-IDF `examples/esp-idf/ai/rtc-tcp-client`: full ESP32-S3 build passed in
  `build-image-decryption`, using an isolated sdkconfig generated from the
  consumer's existing defaults. GCM/key-wiping translation units compiled and
  firmware linked; no flashing or runtime memory measurements were performed.
- At that revision, existing CI invoked the modified suites without new CMake
  registration or CI source-list changes; each suite's main called the added cases.

Release notes are deferred until a real PR number exists; do not fabricate one.
Document the enabled implementation separately from pending validation; do not
claim live-cloud verification.
