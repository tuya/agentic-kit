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
FIRST/MIDDLE/LAST, reassembled on receive before decoding.
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
(default 60 s) and treats the Connection as dead if no Pong arrives within
`ping_timeout_ms` (default 90 s), then fires `on_disconnect`.
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
