# Tuya LAN (Device ↔ App, Local Control)

An application-driven Tuya LAN 3.5 service for an activated device. It owns
discovery, TCP Sessions, authenticated Frames, local DP control and replies.
It does not own the device's schema or DP state: the application connects its
callbacks to the [IoT Client](../iot-client/CONTEXT.md).

## Language

**Discovery**: encrypted UDP announcements and App discovery requests. The
published discovery key identifies the protocol; it does not authorize control.
_Avoid_: Pairing (BLE), Activation (cloud).

**Session**: one accepted App TCP connection, with its own handshake, session
key, sequence counters, receive buffer and send queue. It is unrelated to an
AI Foundation Session. _Avoid_: MQTT connection, TAI Session.

**Frame**: LAN 3.5 header, nonce, AES-GCM ciphertext/tag and tail. Its header
is authenticated additional data. _Avoid_: MQTT envelope, BLE Packet.

**local_key**: the 16-byte credential returned by Activation, used to
authenticate the three-step LAN handshake. _Avoid_: secret_key (MQTT/cloud
authentication), auth_key (the factory credential), AI session token.

**Session key**: the per-Session AES-GCM key derived from the two handshake
nonces after mutual HMAC validation. Its first byte may be zero; only the
explicit authenticated state establishes readiness.

**Local DP control**: an authenticated App request to apply a `dps` object.
The application supplies the schema and controls execution through the public
`iot_client_apply_local_dps()` interface or its own callback implementation.
_Avoid_: cloud downlink (which has different dirty/reporting semantics).

**Report**: an application-requested LAN status notification to authenticated
Sessions. It neither publishes MQTT nor clears the cloud's dirty bits.

**Extension**: authenticated opaque data identified by a LAN command, including
`0x20` for P2P signaling. A signaling callback does not implement P2P media.

## Invariants

- LAN is opt-in at build time: CMake `AGENTIC_KIT_ENABLE_LAN` and ESP-IDF
  `CONFIG_AGENTIC_KIT_ENABLE_LAN` both default off. The build exports the
  `AGENTIC_KIT_ENABLE_LAN` availability macro as 0 or 1 to SDK consumers.
  Disabled builds omit LAN and its socket port, tests and example; the IoT
  local DP ingress remains available. Runtime start/stop controls listening
  only after LAN has been included in the build.
- One application owner drives start/stop/process/report/send and every callback.
  There is no worker, sleep, automatic MQTT publication or reconnect. Callbacks
  may send/report; teardown and recursive process calls must wait until they return.
- Initialize the process-wide PAL/RNG/cJSON binding once before JSON processing
  (`iot_init()` already does this). All SDK contexts use that same allocator.
- `pal_t` is the single platform capability table. Its optional server/datagram
  callbacks are required only for LAN and checked by `pal_has_server_network()`.
  The fields remain present in disabled builds; base `pal_is_valid()` does not
  require them. Accepted TCP connections share the existing TCP I/O and close
  callbacks, using timeout zero in the LAN loop. LAN I/O is nonblocking; partial
  sends and backpressure retain queued bytes until accepted or the Session closes.
- Only security commands 3 and 5 use local_key before authentication. DP,
  heartbeat and extensions require an authenticated Session; there is no
  compatibility allowance for unauthenticated local_key commands.
- Verify Frame geometry and GCM authentication before committing sequence or
  application state. Replayed Frames, invalid authentication and oversize
  Frames close only their own Session. Entropy failure cannot establish one.
- Handshake and liveness deadlines use monotonic `pal_t.time_ms`; report time
  uses UTC `pal_t.wal_clock_time`. The application owns UTC synchronization.
- Discovery replies use the received source address, validated against the
  request, rather than trusting an arbitrary JSON destination.
- LAN and local DP input share the bounded string/number lexical checks in
  `common/json_lexical.h`: reject invalid UTF-8, malformed escapes, unpaired
  surrogates and decoded NUL before cJSON parsing. JSON structure and DP-key
  rules remain with the consumers.
- Config strings outlive the service. Stop/destroy LAN before clearing,
  replacing or freeing activation credentials; recreate it after a key change.
- LAN and cloud share one DP state. Apply local controls with dirty=true,
  persist through the existing save callback, and let the application decide
  when to report to each channel. Query/report never clear MQTT dirty bits.
- The existing DP parser does not enforce schema access mode. Product-specific
  authorization belongs in the application callback before applying a request.

## Reference baseline

The wire format is based on TuyaOpen commit
`68f76c966ef6154c319ce122506d4e0934b2979b`, `src/tuya_cloud_service/lan/` and
`src/tuya_cloud_service/protocol/tuya_protocol.c`. This module intentionally
uses agentic-kit's PAL and application-owned lifecycle. It implements LAN 3.5;
legacy LAN versions, AP provisioning and P2P media are separate capabilities.
