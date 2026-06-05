# Tuya BLE (Device ↔ App, WiFi Provisioning)

The device-side context that hands a device its WiFi credentials over Bluetooth Low Energy:
the device advertises, the phone app connects, the two run an encrypted pairing handshake,
and the app delivers SSID / password / token. This is the on-boarding transport that runs
*before* the device can reach the cloud — once it has credentials and joins WiFi, the
[IoT Client](../iot-client/CONTEXT.md) takes over for activation.

## Language

**Provisioning**:
The whole job of this context: delivering WiFi credentials to the device over BLE so it can
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
UUID (`0xFD50`), and the `product_key`.
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
The static Tuya vendor identifier for the product *family*, embedded in the advertising
data so the app knows what kind of device this is.
_Avoid_: uuid (per-device), auth_key (the secret), schema id (an IoT-Client term).

**uuid**:
The per-device identifier string (16 or 20+ chars) supplied in config; the source the BLE
ID is derived from.
_Avoid_: BLE ID (the derived 16-byte value), product_key.

**auth_key**:
The 32-byte per-device shared secret, the root from which the encryption keys are derived
via MD5.
_Avoid_: key (unqualified), token, local_key (an IoT-Client term).

**pair_rand**:
The 6-byte random nonce the *device* generates and sends during device-info exchange; an
input to `key_12`.
_Avoid_: server_rand (the app's nonce), IV, salt.

**server_rand**:
The 16-byte random value the *app* sends; cached by the device as the IV and used as an
input to `key_11`.
_Avoid_: pair_rand (the device's nonce), seed.

**key_11**:
The first encryption key, `MD5(auth_key ‖ uuid ‖ server_rand)`, used for the early pairing
frames (device-info). Mode byte `0x0B`.
_Avoid_: key_12 (the later key), session key, auth_key (its input).

**key_12**:
The second encryption key, `MD5(key_11 ‖ pair_rand)`, used once pairing completes (pair
response, net-status, WiFi config). Mode byte `0x0C`.
_Avoid_: key_11 (the earlier key).

**Encryption mode**:
The one-byte selector at the front of a Packet choosing the cipher: `NONE` (`0x00`),
`KEY_11` (`0x0B`), or `KEY_12` (`0x0C`). It names *which key*, not the key itself.
_Avoid_: encryption key (the material), cipher suite.

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
`sn` is the device's outgoing per-message counter stamped in the Frame header; `last_rx_sn`
is the last sn received, echoed back as `ack_sn`.
_Avoid_: trsmitr seq (the link-chunk counter — a different counter), index, offset.

**Provisioning callback** (cb):
The app-supplied function the device invokes once WiFi credentials are parsed, handing over
the creds for the application to act on.
_Avoid_: handler, listener, hook.

### Flagged ambiguities

- **"key"** is badly overloaded: *auth_key* (the 32-byte root secret), *product_key* (the
  vendor family id, not secret), *key_11* / *key_12* (the derived encryption keys), and the
  creds *token*. Never write "key" unqualified.
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

## Example dialogue

> **Dev:** The bulb is broadcasting — what's actually in that advertisement?
> **Expert:** The adv_data: flags, the Tuya service UUID `0xFD50`, and your product_key, so
> the app recognises the product family. When the app scans for more, it gets the rsp_data —
> the encrypted BLE ID plus the device name.
> **Dev:** Then the app connects and we're paired?
> **Expert:** Not yet. The app sends a device-info request; we reply with our pair_rand,
> encrypted under key_11 — that's `MD5(auth_key ‖ uuid ‖ server_rand)`, where server_rand is
> the IV the app just gave us. Only after the app sends a pair request whose BLE ID matches
> ours do we set `paired = true` and answer under key_12.
> **Dev:** And the actual WiFi details?
> **Expert:** Those come last, on a downlink-transparent frame encrypted with key_12 — JSON
> with ssid, password, and token. We parse it into the creds and fire the provisioning
> callback. That's the moment provisioning succeeds; pairing was just the handshake that got
> us a trusted key.
> **Dev:** Why does a single message sometimes arrive in pieces?
> **Expert:** That's Trsmitr — the link layer splits a Packet bigger than the BLE MTU into
> sequenced segments and reassembles them before we ever see the Frame. Don't confuse its
> per-segment seq with the Frame's `sn`/`ack_sn`, which order whole messages.
