# Tuya LAN (Device ↔ App, Local Control)

An application-driven Tuya LAN 3.5 service for an activated device. It owns
discovery, TCP Sessions, authenticated Frames, local DP control and replies.
It does not own the device's schema or DP state: the application connects its
callbacks to the [IoT Client](../iot-client/CONTEXT.md).

## Language

**Discovery**: encrypted App discovery requests and device replies, with optional
UDP announcements. The published discovery key identifies the protocol; it does
not authorize control.
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
- Every error associated with a Session immediately closes its socket: protocol,
  Frame/GCM/replay, JSON/schema, callback, resource, unsupported command and local
  report/send parameter or payload-size errors. The only recoverable socket
  result is PAL_ERR_AGAIN (would-block or interruption); positive partial I/O is
  progress, not an error. Error paths close without queuing error ACKs.
- A send error affects its identified Session; an unknown ID cannot close
  unrelated Sessions. An invalid broadcast report closes its authenticated
  targets; a Session-specific encoding or queue failure closes only that Session.
  Malformed UDP discovery packets have no TCP Session association and are dropped.
- Socket closure is immediate even inside a callback. Protocol and queue storage
  stays alive until active process/callback stacks unwind, preventing use-after-free;
  outside those stacks a failed Session is reclaimed before the API returns.
- Before authentication, only the expected START (3) or FINISH (5) is accepted
  with local_key, according to handshake state. The first non-handshake Frame,
  any TYPE4, and duplicate/out-of-order handshake commands close that Session.
  There is no pre-authentication skip budget or TYPE4-ignore allowance. DP,
  heartbeat and extensions require an authenticated Session.
- Verify Frame geometry and GCM authentication before committing sequence or
  application state. Replayed Frames, invalid authentication and oversize
  Frames close only their own Session. Entropy failure cannot establish one.
- Handshake and liveness deadlines use monotonic `pal_t.time_ms`; report time
  uses UTC `pal_t.wal_clock_time`. The application owns UTC synchronization.
- Discovery replies use the received source address, validated against the
  request, and the App's fixed UDP port 7000, independently of the device's
  configured listener port or the request's source port.
- Discovery supports both App queries and periodic announcements. The first
  process call attempts an announcement, then the default interval of 5000 ms
  sends to 255.255.255.255:6667. Announcements are an agentic-kit extension
  enabled by default, unlike the reference's query-only behavior. Setting
  `discovery_interval_ms = 0` or `broadcast_ip = NULL` disables announcements
  while query replies remain available.
- Successful controls receive a big-endian 32-bit return code 0; failed controls
  close the Session without an error ACK. Explicit send return codes retain their
  big-endian 32-bit encoding. This differs from the pinned TuyaOpen control path's
  constant native-order value 1 and its pre-authentication compatibility allowances.
- Query callbacks supply `{"dps":{...}}`; LAN adds `devId` from
  `config.device_id` to the wire response. Callback data cannot override that
  configured identity.
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

The compatibility decisions and intentional differences are recorded in
[ADR 0002](docs/adr/0002-lan-wire-compatibility-and-application-contracts.md).
Canonical DP IDs and strict JSON lexical rules are agentic-kit input policy,
as described in [ADR 0001](docs/adr/0001-lan-is-application-driven-and-shares-dp-state.md),
and do not claim byte-for-byte equivalence with TuyaOpen's cJSON ingress.
