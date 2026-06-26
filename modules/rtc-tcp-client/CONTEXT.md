# RTC TCP Client (Device ↔ Tuya AI Foundation)

The device-side context that opens a real-time audio/video/text conversation with the
Tuya AI Foundation server over a TCP+TLS link, then streams media and events both ways
in a custom binary framing. This is transport, not business state — it carries turns of a
conversation, it does not own device DPs (that is the [IoT Client](../iot-client/CONTEXT.md)).

## Language

**Connection**:
The single TCP+TLS link to the AI server, opened by `tai_connect` (TLS handshake →
ClientHello → SessionNew → start the background receive thread) and torn down by
`tai_disconnect`. One Connection carries one Session at a time.
_Avoid_: socket, channel, link (reserve those for the raw transport).

**Session**:
The logical conversation opened over a Connection, identified by a `session_id`
(`vcd-session-<hex>`). Created by a SessionNew packet, ended by SessionClose. It is the
scope that Events live inside.
_Avoid_: connection (the network link), conversation, channel.

**Event**:
One turn of work inside a Session, identified by an `event_id` (`vcd-event-<hex>`), with a
fixed lifecycle: EventStart → payloads → EventPayloadsEnd → EventEnd. Only one Event is
open at a time.
_Avoid_: request, turn, message, transaction.

**Packet**:
An application-layer protocol unit — ClientHello, SessionNew, Event, Text, Audio, Image,
Ping/Pong, etc. — encoded as `[header byte][attribute block][payload]`. Identified by a
*packet type* (`TAI_PKT_*`).
_Avoid_: message, frame (the wire unit), datagram.

**Frame**:
The wire-level unit a Packet is sent inside: `[flags][seq:2][length:2][payload][signature]`.
One Packet maps to one or more Frames; the signature is 0/20/32 bytes per *sign level*.
_Avoid_: packet (the application unit), segment, chunk.

**Attribute** (attr):
A `[type:2][len:2][value]` tuple carrying metadata inside a Packet (session-id, event-id,
audio-params, user-data, …), grouped in a length-prefixed attribute block. Identified by an
*attribute type* (`TAI_ATTR_*`).
_Avoid_: field, header, tag, property.

**Fragmentation**:
Splitting one oversized Packet across multiple Frames (≤32 KB each) marked
FIRST/MIDDLE/LAST, reassembled in the receive buffer (`frag_buf`) until LAST and then
decoded as one whole Packet. Outbound, any oversized Packet is fragmented uniformly.
_Avoid_: chunking (that is the stream-level term), segmentation.

**Stream flag**:
The position of a media/text chunk within its flow: `ONE_SHOT`, `START`, `MIDDLE`, or
`END` (`TAI_STREAM_*`). A spoken utterance is START + MIDDLE* + END; a one-line text is
ONE_SHOT.
_Avoid_: event type (a different axis — see ambiguities), fragment flag (that is transport).

**Data ID**:
A `uint16` tag multiplexing channels within an Event — `AUDIO_UP`/`AUDIO_DOWN`,
`TEXT_UP`/`TEXT_DOWN`, `IMAGE_UP` (`TAI_DATA_ID_*`). `_UP` is device→server, `_DOWN` is
server→device.
_Avoid_: data type, channel id (informal), stream id.

**Event type**:
A `uint16` semantic code for *what happened* (`TAI_EVT_*`): Start, PayloadsEnd, End,
ChatBreak, ServerVAD, MCPCmd, ServerTimeover, … Delivered to `on_event`.
_Avoid_: packet type (the wire kind), stream flag (chunk position), command.

**Sign level**:
How each Frame is authenticated: `NONE`, `HMAC_SHA1` (20-byte sig), or `HMAC_SHA256`
(32-byte sig) (`TAI_SIGN_*`). ClientHello is the one Frame sent unsigned.
_Avoid_: encryption (signing ≠ confidentiality), auth mode.

**Keepalive (Ping / Pong)**:
The liveness exchange the background thread runs: it sends a Ping every `ping_interval_ms`
(default 60 s) and treats the Connection as dead if no inbound traffic — a Pong *or any*
received data — arrives within `ping_timeout_ms` (default 90 s), then fires `on_disconnect`.
Counting any receive, not just Pong, keeps a long downstream stream from tripping a spurious
timeout (see [ADR 0001](docs/adr/0001-receive-worker-callback-greedy-nest.md)).
_Avoid_: heartbeat, poll.

**Chat break**:
A client-sent Event (`TAI_EVT_CHAT_BREAK`) that interrupts the server's in-progress
response. Sent standalone, not part of an Event's normal lifecycle.
_Avoid_: cancel, stop, abort.

**Server VAD**:
A server-sent Event (`TAI_EVT_SERVER_VAD`) signalling that voice-activity detection found
the end of the user's speech in audio mode.
_Avoid_: silence detection, endpointing.

**MCP command**:
A Model-Context-Protocol request the server sends as an Event (`TAI_EVT_MCP_CMD`, 1000),
carrying JSON-RPC 2.0. The app executes it and replies with `tai_send_mcp_response`.
_Avoid_: tool call, function call, RPC (too generic).

### Flagged ambiguities

- **"type"** is overloaded across four axes: *packet type* (`TAI_PKT_*`, the wire kind),
  *event type* (`TAI_EVT_*`, the semantic), *attribute type* (`TAI_ATTR_*`), and
  *client type* (Device vs App). Always qualify it.
- **"id"** is overloaded: *session-id* and *event-id* are strings; *data-id* is a `uint16`
  channel tag; *client-id* is the device id. Name which one.
- **"state"** is overloaded: *fragment state* (mid-reassembly), *event-open* (an Event is
  in progress), and *connection state* (connected / session-open) are distinct.
- **Packet vs Frame** is the layer boundary: a Packet is application-level (Text, Event); a
  Frame is what crosses the wire (with seq + signature). One Packet → one-or-more Frames.
- **Stream flag vs Event type** are different axes: stream flag is a chunk's position within
  one data channel; event type is the semantic of an Event. ChatBreak is an event type, not
  a stream flag.

## Data flow

### Layers and threads

Top to bottom: the send API (`tai_send_*`) → Packet builders (`tai_proto_build_*`) → the
Frame layer — encoding, the HMAC signature, and Fragmentation (`tai_transport.c`) → an I/O
abstraction (`ctx_io_send` / `ctx_io_recv`, routing to TLS or, in test mode, raw TCP) → the
PAL. Two threads run at once: **caller threads** drive every `tai_send_*`; one **background
worker** (started by `tai_connect`, joined by `tai_disconnect`) drives receiving and
Keepalive.

Locking: a single ctx mutex serialises the send side — each `tai_send_*` holds it
for its whole sequence, so one Event's Packets, and the bytes of one Frame, never interleave
with another sender (including the worker's Ping). The receive buffers are touched only by
the worker, so receiving — and the user callbacks dispatched from it — is lock-free; mbedTLS
read and write are serialised inside the TLS layer, so the worker can read while a caller
writes. (The shared PAL mutex is *recursive*, but TAI does not rely on that — the recursion
exists for the IoT DP layer; see [ADR 0001](docs/adr/0001-receive-worker-callback-greedy-nest.md).)

### Sending

There are two send paths, split by packet kind:

Every packet — control and media — streams **scatter-gather** through `send_app_sg`, so there is
no large contiguous frame buffer:

- **Media packets** (audio, image, large text, MCP JSON): a `*_hdr` builder writes only the small
  application header into `tx_hdr_buf+5`, and the large payload stays in the caller's buffer.
- **Control packets** (SessionNew/Close, Event start/payloads-end/end, ChatBreak, Ping): the whole
  application packet is assembled in `tx_ctrl_buf` (its attribute block can carry the user
  session/event JSON) and handed to `send_app` → `send_app_sg` with no application header — the
  packet itself is the zero-copy payload.

`send_one_frame_sg` signs the logical `[frame header || app header || payload]` via
`tai_frame_hmac_sg` (byte-identical to the contiguous HMAC — the receiver is unchanged), then
writes the merged `[frame header || app header]`, the **zero-copy payload**, and the signature
(2–3 TLS records per Frame). A logical packet over 32 KB is fragmented across the concat, with the
app header only in the first Frame. (ClientHello is the one exception: it is sent *unsigned* and
one-shot, so `tai_connect` frames it inline on the stack rather than through the signing sender.)

Sequence numbers come from `tai_next_seq` (monotonic, wrapping 65535→1). A failure *before* any
byte hits the wire (a build/encode error) rolls the counter back and returns that error. A
failure *once writing has started* desyncs the stream unrecoverably — and `ctx_io_send`
(`tls_write` / raw TCP) is **not** all-or-nothing, so even a control packet's first write can
leave a half-frame — so it returns `TAI_ERR_NET` (distinct from the pre-wire errors; the sequence
is *not* rolled back). That is a synchronous error the caller acts on: an app sender should
`tai_disconnect` + reconnect, and the worker's own periodic Ping is its TX health probe — a failed
Ping send makes the worker fire one `on_disconnect(TRANSPORT, NET_ERROR)` (§6.3), which also catches
a broken uplink while the app is idle. There is no separate "broken" latch; the return value is the
signal. Higher-level senders compose Packets into an Event:

- **Text** (`tai_send_text`): EventStart → Text (Stream flag ONE_SHOT, Data ID `TEXT_UP`) →
  EventPayloadsEnd → EventEnd.
- **Audio** (`tai_send_audio_start` / `_chunk` / `_end`): start opens the Event and caches the
  codec params; the first chunk carries Stream flag START plus the `audio-params` Attribute,
  later chunks MIDDLE; end sends an Audio END, then EventPayloadsEnd and EventEnd.
- **Image** (`tai_send_image`, `tai_send_image_with_text`): EventStart → (optional Text) →
  Image (ONE_SHOT) → EventPayloadsEnd → EventEnd.
- **Standalone**: `tai_ping`, `tai_chat_break`, and `tai_send_mcp_response` are each one Packet.

ClientHello is the one Packet sent at Sign level NONE (unsigned); everything after SessionNew
is signed.

### Receiving

The worker loops: check the liveness deadline, send a Ping when due, then block in
`tai_recv_data` until bytes arrive or the next Ping falls due, then drain. The drain is
time-bounded (`TAI_DRAIN_BUDGET_MS`, default 150 ms) so a sustained downstream flood cannot
starve the Ping / liveness / shutdown checks — leftover bytes wait for the next pass; and any
successful receive refreshes the liveness clock. Bytes accumulate in a sliding receive buffer;
EOF or a transport error makes the worker fire `on_disconnect`.

`tai_process_rx` peels complete Frames off the front of that buffer:

1. Confirm the leading byte looks like a v2.1 Frame; if not, fail-fast (on a reliable ordered
   TLS stream a desync cannot be recovered by dropping bytes).
2. Read the length field for the full Frame size; wait if the whole Frame has not arrived. A
   Frame that declares more bytes than the receive buffer can hold (`rx_buf`, one max Fragment)
   can never be assembled, so it is fail-fast (`TAI_PROTO_ERR_OVERSIZED`) rather than stalling
   until the liveness timeout — this only trips if the server ignores the advertised
   MAX_FRAGMENT_LEN.
3. Verify the HMAC; a mismatch is fail-fast (tear down and reconnect).
4. Decode the header, then reassemble Fragmentation: `FRAG_NONE` is complete as-is;
   FIRST/MIDDLE/LAST accumulate into the reassembly buffer (`frag_buf`) until LAST, then the
   whole Packet is decoded once. A truncated/oversized fragment, an orphan MIDDLE/LAST, an
   overflow, or a decode failure is **fail-fast** — the worker returns the cause and tears the
   Connection down (the app reconnects); it does not resync or drop frames.
5. Dispatch the complete Packet *before* consuming the Frame — a `FRAG_NONE` Packet points into
   the receive buffer, which the consume step then slides forward.

`tai_proto_dispatch` routes complete Packets by type: **Pong** feeds Keepalive; **Audio** strips
the media header, parses `audio-params` once per stream (sample rate, frame size; re-read on each
START), splits concatenated constant-bitrate Opus by frame size, and delivers each frame to
`on_audio`; **Text** strips the text header and delivers to `on_text` with its Stream flag;
**Event** unpacks the Event type and data (EventEnd clears the open Event) and delivers to
`on_event` — where ServerVAD, MCP commands, etc. surface; **ConnectionClose / SessionClose**
clear state and fire `on_disconnect`. An **unknown Packet type or Event type** (framing and HMAC
valid, but the type is not enumerated) is *tolerated*: it is logged and skipped so the link stays
up — a server that introduces a forward-compatible new type must not knock existing clients
offline. (A *malformed* Packet/Event — decode failure — is still fail-fast; only unknown-but-well-
formed types are skipped.) `on_disconnect` is single-point for **terminal** causes
(transport / protocol / ConnectionClose): the guard fires those at most once per Connection. A
server SessionClose is *non-terminal* (`connection_alive=1`, the link may persist for a new
session) — it fires from the dispatch path but does **not** latch the guard, so a real transport
death that later tears the same Connection down is still delivered as a second, distinct
`on_disconnect`.

### Connection lifecycle

`tai_connect`: generate the security-suite random → derive the encrypt and sign keys → open the
Connection (TLS handshake) → send the unsigned ClientHello → send SessionNew → mark the
Connection connected and the Session open → start the worker.

`tai_disconnect`: stop and join the worker first → then, **all under the send lock**, send
SessionClose, close the transport, and reset the receive/reassembly buffers and the connected /
session-open / event-open flags. Holding the lock across the transport close (not just the
SessionClose) is what makes it safe against a concurrent sender on another thread: that sender
only touches the socket under the same lock, and the close nulls the transport handle under the
lock, so a sender that races in afterwards sees a null handle and fails cleanly instead of using
freed memory. Because it joins the worker, it must run on a thread *other* than the callbacks
(which run on that worker) — calling it from inside a callback self-deadlocks.

`tai_request_disconnect`: the callback-safe way to stop. It only sets the worker's stop flag
(no join), so it is safe from any thread including a receive callback; the owning thread must
still call `tai_disconnect` afterwards to join and release. Receive callbacks therefore have a
re-entrancy contract — may call `tai_send_*`, must not call `tai_disconnect` / `tai_ctx_deinit`,
must not block — captured in [ADR 0001](docs/adr/0001-receive-worker-callback-greedy-nest.md)
and `tuya_ai.h`.

### Invariants

- The receive buffer must hold the largest single Frame; because the server fragments anything
  large, a single Frame stays well within it.
- Reassembly trusts FIRST/MIDDLE/LAST ordering — safe over the reliable, in-order Connection.
- ClientHello is the only unsigned Frame.

## Example dialogue

> **Dev:** I called `tai_connect` and it returned OK — am I in a Session yet?
> **Expert:** Yes. `tai_connect` opens the Connection (TLS), sends an unsigned ClientHello,
> then a signed SessionNew, and starts the receive thread. After that you have one Session
> (`vcd-session-…`) ready for Events.
> **Dev:** So `tai_send_text("hi")` is one packet?
> **Expert:** No — it's a whole Event: EventStart, a Text packet on data-id `TEXT_UP` with
> stream flag ONE_SHOT, then EventPayloadsEnd and EventEnd. Each is an application Packet,
> wrapped in a Frame with a sequence number and an HMAC signature at your sign level.
> **Dev:** The reply comes back on `on_text`?
> **Expert:** Right — server text arrives on `on_text` with a stream flag (START…END for a
> streamed answer), audio on `on_audio`, and everything else on `on_event`: ServerVAD,
> EventEnd, and MCP commands.
> **Dev:** When the server asks me to run a tool?
> **Expert:** That's an `on_event` with event type MCPCmd (1000) carrying JSON-RPC. You run
> it and reply with `tai_send_mcp_response`. To cut the model off mid-answer, send a chat
> break — that's a different event type, not a stream flag.
> **Dev:** And if the link goes quiet?
> **Expert:** The receive thread pings every 60 s; no pong within 90 s and it fires
> `on_disconnect`. Call `tai_disconnect` to send SessionClose + ConnectionClose and stop the
> thread.
