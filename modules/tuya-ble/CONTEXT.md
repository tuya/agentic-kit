# Tuya BLE (Device ↔ App, Provisioning and Bound Control)

The device-side context that hands a device its WiFi credentials over Bluetooth Low Energy:
the device advertises, the phone app connects, the two run an encrypted pairing handshake,
and the app delivers SSID / password / token. This is the on-boarding transport that runs
*before* the device can reach the cloud — once it has credentials and joins WiFi, the
[IoT Client](../iot-client/CONTEXT.md) takes over for activation.

After Activation, the application may initialize this same context with saved
`local_key` and `secret_key` to enable **Bound control**. That mode advertises an
already activated device, authenticates the phone with different derived keys,
and delivers DP write/query requests to application callbacks. It does not run
the IoT Client or own the DP registry. The application composes BLE, LAN and
MQTT around its existing product state.

## Language

**Provisioning**:
Delivering WiFi credentials to the device over BLE so it can
get onto the network. Succeeds when the provisioning callback fires with valid creds.
_Avoid_: pairing (that is one step inside it), binding, registration (those are cloud-side).

**Pairing**:
The cryptographic handshake that establishes the shared encryption keys and confirms the
device identity; on success the device sets `paired = true`. A prerequisite for, not a
synonym of, provisioning.
_Avoid_: provisioning (the goal), bonding, the BLE-stack sense of "pair".

**WiFi credentials** (creds):
The bundle the device is provisioned with — `ssid`, `password`, and `token` — extracted
from the app's downlink JSON and passed to the callback.
_Avoid_: config, payload, network info.

**Token**:
The short (≤16-char) one-time provisioning token inside the creds, used later by the cloud
to bind the device to the user's account.
_Avoid_: key, secret, auth_key (a different credential — see ambiguities).

**Advertising data** (adv_data):
The ≤31-byte BLE advertisement the device broadcasts unsolicited: flags, the Tuya service
UUID (`0xFD50`), and the `product_key` during Provisioning. Bound control replaces
the product field with a credential-derived encrypted identifier and sets bound flags.
_Avoid_: scan response (the on-demand reply), beacon, broadcast payload.

**Scan response data** (rsp_data):
The separate ≤31-byte payload the device returns when a scanner asks for more: the
encrypted BLE ID, the device name, and the Tuya company ID (`0x07D0`).
_Avoid_: advertising data (the unsolicited broadcast), response packet.

**BLE ID**:
The 16-byte device identifier derived from the `uuid` (used directly when short, compressed
when long) that the app must match during Pairing.
_Avoid_: uuid (the input string), device id, MAC address.

**product_key**:
The static Tuya vendor identifier for the product *family*, embedded in Provisioning
advertising data so the app knows what kind of device this is. Validated as a 16-byte
NUL-terminated string before copying into advertising data.
_Avoid_: uuid (per-device), auth_key (the secret), schema id (an IoT-Client term).

**uuid**:
The per-device identifier string supplied in config: exactly 16 bytes, or 20 alphanumeric
bytes compressed into a BLE ID. The source identifier, not the derived BLE ID.
_Avoid_: BLE ID (the derived 16-byte value), product_key.

**auth_key**:
The 32-byte per-device shared secret, the root from which Provisioning keys are derived
via MD5; its first 16 bytes also key the AES block returned in the device-info response.
Validated as a 32-byte NUL-terminated string at initialization.
_Avoid_: key (unqualified), token, local_key (an IoT-Client term).

**pair_rand**:
The 6-byte random nonce the *device* generates and sends during device-info exchange; an
input to `key_12` or `key_15` for that exchange.
_Avoid_: server_rand (the app's nonce), IV, salt.

**server_rand**:
The 16-byte random value the *app* sends; cached by the device as the IV and used as an
input to `key_11`.
_Avoid_: pair_rand (the device's nonce), seed.

**key_11**:
The first encryption key, `MD5(auth_key ‖ BLE ID ‖ server_rand)`, used for the early pairing
frames (device-info). The identifier input is the derived 16-byte BLE ID, not the original
20-byte UUID when compression is used. Mode byte `0x0B`.
_Avoid_: key_12 (the later key), session key, auth_key (its input).

**key_12**:
The second encryption key, `MD5(key_11 ‖ pair_rand)`, available after device-info exchange
and used for the pair request/response, net-status, WiFi credentials and big-data channel.
Mode byte `0x0C`; possession of this key alone does not mean Pairing has completed.
_Avoid_: key_11 (the earlier key).

**Encryption mode**:
The one-byte selector at the front of a Packet choosing the cipher: `NONE` (`0x00`),
`KEY_11` (`0x0B`), `KEY_12` (`0x0C`), `KEY_14` (`0x0E`), or `KEY_15` (`0x0F`).
It names *which key*, not the key itself.
_Avoid_: encryption key (the material), cipher suite.

**Bound control**:
Local DP control for an activated device initialized with both `local_key` and
`secret_key`. Successful Pairing uses `key_14` / `key_15` and reports pair result 2.
It does not activate, unbind or reset the device.
_Avoid_: Provisioning, BLE bonding, cloud connection.

**Activation credentials** (`local_key`, `secret_key`):
The saved 16-byte, NUL-terminated strings supplied together for Bound control.
Both are borrowed for the BLE state's lifetime. The names refer to the same
Activation artefact used by the IoT Client, not new BLE-only secrets.
_Avoid_: auth_key, provisioning Token, session key.

**key_14**:
The Bound-control device-info key, `MD5(local_key ‖ secret_key)`, mode `0x0E`.
_Avoid_: key_11 (the Provisioning key), local_key (one input).

**key_15**:
The Bound-control Pairing and DP key,
`MD5(local_key ‖ secret_key ‖ pair_rand)`, mode `0x0F`.
Possession alone does not bypass successful Pairing and Frame authorization.
_Avoid_: key_12 (the Provisioning key), key_14 (the device-info key).

**DP TLV**:
A batch of Data Points encoded as DPID (one byte), type (one byte), length
(two bytes, big-endian) and value. Types are raw (0), bool (1), signed value
(2), UTF-8 string (3), enum (4) and bitmap (5). IDs are nonzero and unique
within a batch. BLE validates the representation; the application owns schema,
access policy, hardware effects and the authoritative DP state.
_Avoid_: DP JSON (a conversion format), schema (not carried in the TLV).

**DP callback** (`dp_write`, `dp_query`):
The application boundary for a validated write batch or DPID query. Empty query
means all DPs. Buffers are borrowed only during the callback; return 0 after
copying and accepting work, otherwise a failure ACK is sent. Acceptance is not
proof that the IoT registry or hardware applied the request.
_Avoid_: provisioning callback, automatic cloud dispatch.

**DP report**:
An explicit `tuya_ble_dp_report()` (`0x8006`) or
`tuya_ble_dp_report_with_time()` (`0x8007`) call on the BLE owner. A query-response
flag distinguishes requested state from unsolicited status. The application
supplies timestamped reports' UTC seconds. Neither reports nor query ACKs
publish MQTT, clear MQTT dirty bits or automatically generate another report.
_Avoid_: ACK (acceptance only), cloud report.

**Connection generation** (`connection_generation`):
The application-visible fence that changes on session close and accepted
device-info re-query. Queued DP work and report completions copy their original
generation; the application discards stale completions before using a newer
Pairing exchange. All BLE state access remains on the BLE owner.
_Avoid_: Frame SN, WiFi scan token, synchronization primitive.

**Frame**:
The plaintext protocol unit: a 12-byte header (`sn`, `ack_sn`, `cmd`, `data_len`) + payload
+ 2-byte CRC16. The unit a `cmd` (`FRM_*`) operates on.
_Avoid_: packet (the encrypted wrapper), segment, message.

**Packet**:
The encrypted wrapper actually sent over BLE: `[encryption mode][IV?][ciphertext-of-Frame]`.
_Avoid_: frame (the plaintext inside), trsmitr segment (the link chunk).

**Trsmitr**:
The BLE link-layer segmentation/reassembly scheme that splits a Packet too large for one
GATT write into sequenced sub-chunks and rebuilds it on the other side.
_Avoid_: fragmentation, MTU chunk (informal), framing.

**Sequence number** (sn / last_rx_sn):
`sn` is the device's outgoing per-Frame counter; `last_rx_sn` is the last incoming SN
accepted by the Frame authorization gates, echoed back as `ack_sn`. Acceptance here does
not guarantee that the command's payload was applied.
_Avoid_: trsmitr seq (the Packet counter), subpacket number (the segment counter), index.

**Provisioning callback** (cb):
The app-supplied function that receives valid WiFi credentials after their acknowledgement
and any queued Packets have been accepted by the port's send callback. This is not proof
that the phone received the acknowledgement, nor that WiFi join or cloud activation succeeded.
_Avoid_: handler, listener, hook.

**Big-data channel**:
The encrypted `0x801E` downlink / `0x801F` uplink carrying a two-byte flag, two-byte
subcommand and subcommand payload. WiFi-list and provisioning-status replies carry JSON.
_Avoid_: legacy transparent channel (`0x801B`/`0x801C`), Trsmitr (the outer transport).

**WiFi list**:
Nearby access points returned for subcommand `0x0003`, each described by `ssid`, `rssi`
and `sec`. Entries are ordered strongest-RSSI first within the retained scan results and
limited by the requested count and response size. This is discovery, not WiFi credentials.
_Avoid_: scan response data (BLE discovery), provisioning result.

**Scan provider** (`wifi_scan_request`):
The port-owned asynchronous WiFi scanner. It receives a requested count, optional country
code and scan token; it later hands results back on the BLE owner context. The SDK does
not drive the WiFi radio itself.
_Avoid_: provisioning callback (credential delivery), BLE scanner.

**Scan token** (`wifi_scan_token`):
The nonzero identifier of one outstanding WiFi scan. A completion must carry the token
issued with its request; it must not borrow the current token from a newer request.
_Avoid_: token (the cloud provisioning credential), Frame SN, connection handle.

**Radio capability** (`comm_ability`):
The bands advertised in scan response data and device-info: 2.4 GHz (`0x0004`) and
5 GHz (`0x0008`). Zero selects the 2.4-GHz default; advertising a band does not enable it.
_Avoid_: WiFi-list capability, PSK3.0 support.

**Provisioning status**:
The `type`/`stage`/`status` report on big-data subcommand `0x0004`. The current query
response is fixed at `{"type":1,"stage":0,"status":0}` (CFG); it is not live WiFi-join
or activation progress, and there is no active stage-reporting API.
_Avoid_: net-status (`0x001E`, the separate legacy notification), activation result.

### Flagged ambiguities

- **"key"** is badly overloaded: *auth_key* (the 32-byte root secret), *product_key* (the
  vendor family id, not secret), the Activation credentials *local_key* / *secret_key*,
  *key_11* / *key_12* / *key_14* / *key_15* (the derived encryption keys), and the creds
  *token*. Never write "key" unqualified.
- **"id"** is overloaded: *BLE ID* (16-byte, derived), *uuid* (the input string), and the
  Tuya *company id* (`0x07D0`). Name which.
- **"rand"** is two different nonces from two different parties: *pair_rand* (6 bytes, from
  the device) and *server_rand* (16 bytes, from the app). They feed different keys.
- **Frame vs Packet vs Trsmitr segment** are three nested layers: a Frame (plaintext, with
  CRC) is encrypted into a Packet (with mode + IV), which Trsmitr may split into segments to
  fit the BLE MTU. Use the precise word for the layer you mean.
- **adv_data vs rsp_data** are both ≤31-byte BLE payloads but for different phases: adv_data
  is the unsolicited broadcast; rsp_data is the on-demand scan response.
- **"len"** is overloaded: *data_len* (a Frame's payload size), *enc_pkt_len* (a Packet's
  size), and *rx_total_len* (the full reassembled length across Trsmitr segments). Qualify
  it.

## Invariants

- In Provisioning, credential delivery and inbound big-data require cryptographic
  Pairing and KEY_12. In Bound control those commands are disabled; DP writes
  (`0x0027`) and queries (`0x0003`) require cryptographic Pairing and KEY_15.
  KEY_11/KEY_12 can never authorize Bound control, and Provisioning cannot accept DP
  control. There is no automatic fallback between these modes.
  The compatibility `set_paired(true)` setter cannot grant authorization. Frame bounds,
  CRC, mode, command authorization and a nonzero increasing SN are checked before
  dispatch. Payload validation follows dispatch, so a rejected JSON payload can still
  consume its Frame SN. While credentials await delivery, further credential requests
  are ignored without replacing the pending credentials or enqueueing another ACK.
- Ports must call `tuya_ble_prov_close` on GATT disconnect/host reset. The historical
  `reset_conn` clears transport queues and pending credential/scan delivery but preserves
  Pairing, keys and Frame SN counters; it is not a session close.
- One BLE owner drives RX, TX-ready and monotonic tick. Never call MQTT from this
  context. DP callbacks must not block or reenter BLE state. Copy work to the IoT
  owner before calling `iot_client_apply_local_dps()`; queue explicit reports back
  to the BLE owner. Send callbacks copy before returning: 0 accepted, 1 busy,
  other failure.
- Trsmitr continuation segments contain only subpacket number and data. Version and
  Packet sequence appear in the first segment only; Frame SN is a separate counter.
- TX adds CBC padding only to non-aligned Frames. RX accepts block-aligned ciphertext
  and locates the Frame/CRC using the declared `data_len`; bytes after the CRC are
  ignored, not validated as padding or stripped. This accepts the real app's extra
  full padding block on an aligned Frame. Never infer Frame length from its last byte.
- Trsmitr uses bounded per-state reassembly and a four-Packet TX queue. Notifications
  fit both the GATT payload budget (default 20 bytes) and the peer's PacketMaxSize.
  The port drives TX-ready/tick retries after busy; tick discards incomplete RX after
  10 seconds and closes session state after a stalled TX reaches 10 seconds.
- Ports supply a full-fill CSPRNG. Optional `random_fn` reports the number of bytes
  filled; partial output fails the exchange. The legacy void HAL cannot report errors.
- Config strings are borrowed, NUL-terminated and validated: product_key 16 bytes,
  auth_key 32 bytes, UUID 16 bytes or 20 alphanumeric bytes. Both Activation
  credentials absent means Provisioning; both present and exactly 16 bytes means
  Bound control. A partial or malformed pair fails initialization. Stop the port
  before reinitializing with different Activation credentials, and discard work
  queued for the old state. Close wipes session material while retaining borrowed
  configuration for reconnect. Recompile consumers when
  the public state layout changes. Initialize PAL/cJSON hooks with `iot_init()` before
  JSON parsing; never create a second allocator binding.
- In Provisioning, a configured scan provider enables scan-response flag bit 5 and appends the
  device-info capability tail `00 01 03` (CombosFlag bits 0 and 1: WiFi list and
  provisioning status). Without a provider these additions are absent; `comm_ability`
  alone does not enable them. The NimBLE example configures a provider. These advertised
  bits do not establish support for a complete PSK3.0/fallback activation exchange:
  credential delivery still uses legacy `0x801B` subcommand `0x0001`.
- Big-data uplinks set flag bit 0 (`0x0001`, response requested). Downlinks with flag
  bit 1 set (big-data segmentation) are rejected; this is separate from supported
  Trsmitr segmentation. Only subcommands `0x0003` and `0x0004` are handled.
- Only one WiFi scan may be outstanding. Missing providers, scan-start failures and
  concurrent queries produce an empty list; a concurrent query leaves the existing
  scan pending. Invalid/missing `cnt` defaults to 10, positive values clamp to 20.
  Completion retains at most the first 20 APs, sorts them by RSSI, then limits the
  count and fits whole entries into a 974-byte JSON budget. SSIDs are JSON-escaped.
- Scan completions must run on the BLE owner context with their original token.
  The SDK rejects mismatched tokens and invalidates pending scans on close,
  transport reset and accepted device-info re-query. Radio cancellation and safe
  cross-task result delivery remain port responsibilities. There is no SDK scan
  deadline; a provider that never completes leaves the scan pending until reset.
- The NimBLE port owns a WiFi scan worker; the SDK itself still has no worker. Request
  and result queues copy the original token with the data. The BLE-owner tick drains
  results; the worker never accesses SDK state or the NimBLE event queue. Cancellation
  discards delivery but occupies the scan slot until the radio operation returns.
  Application-side stop waits for the BLE owner and worker before releasing resources;
  a stalled radio operation can therefore delay stop. Before joining WiFi, the demo
  stops the scan radio and starts STA with credentials to get a fresh STA_START event.
- An accepted device-info re-query clears Pairing/authorization and generates a new
  pair_rand. It still requires an increasing Frame SN and an empty TX queue; it does
  not reset SN counters or cancel the port's radio operation. Session close and
  accepted device-info re-query change `connection_generation`; queued report
  completions must not cross that boundary.
- DP TLV validation rejects a malformed batch before invoking the application.
  `tuya_ble_dp_to_json()` converts a valid batch to bare DP JSON for the public
  IoT local-DP input, using Base64 for raw values. That input applies schema
  validation and marks accepted state dirty, but does not publish. The current
  IoT registry has no bitmap type: a product may handle bitmap TLVs directly in
  its BLE callback instead of feeding them to that registry. Query requests and
  writes do not automatically report state.

## Example dialogue

> **Dev:** The bulb is broadcasting — what's actually in that advertisement?
> **Expert:** The adv_data: flags, the Tuya service UUID `0xFD50`, and your product_key, so
> the app recognises the product family. When the app scans for more, it gets the rsp_data —
> the encrypted BLE ID plus the device name.
> **Dev:** Then the app connects and we're paired?
> **Expert:** Not yet. The app sends a device-info request; we reply with our pair_rand,
> encrypted under key_11 — that's `MD5(auth_key ‖ BLE ID ‖ server_rand)`, where server_rand is
> the IV the app just gave us. Only after the app sends a pair request whose BLE ID matches
> ours do we set `paired = true` and answer under key_12.
> **Dev:** And the actual WiFi details?
> **Expert:** Those come last, on a downlink-transparent frame encrypted with key_12 — JSON
> with ssid, pwd, and token. We parse it into the creds and acknowledge it; after the port
> accepts the queued Packets, we fire the provisioning callback. WiFi join and activation
> still belong to the application; Pairing only established authorization.
> **Dev:** Why does a single message sometimes arrive in pieces?
> **Expert:** That's Trsmitr — the link layer splits a Packet bigger than the BLE MTU into
> sequenced segments and reassembles them before we ever see the Frame. Don't confuse its
> per-segment seq with the Frame's `sn`/`ack_sn`, which order whole messages.
