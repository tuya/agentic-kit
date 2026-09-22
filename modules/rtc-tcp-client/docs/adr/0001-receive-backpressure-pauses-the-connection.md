# Receive backpressure pauses the Connection and retains a partial Packet in place

The RTC TCP Client exposes an optional `on_flow_control` admission hook. When it
returns zero, the worker stops parsing and stops reading the Connection, allowing
the TCP receive window to close. This pauses all inbound traffic rather than
selecting only audio. The worker still runs Ping, shutdown, and bounded admission
checks through `pal_t.sleep_ms`.

Admission is checked between complete Frames and before each codec-frame callback
within an Audio Packet. Checking only at Frame boundaries would force the
application either to accept a whole Packet or to lose its remainder: the queue
that fills is the application's, and one Audio Packet can emit several codec frames.

A mid-Packet pause keeps the Packet's bytes where they already are rather than
copying them into a second queue. The worker records the remaining body pointer, its
length, and the wire length of the pinned Frame; only the worker advances that
cursor, and no other Packet dispatches until it is exhausted — so `rx_audio_*` and
`rx_event_id` still describe the paused Packet and need not be duplicated. Teardown
discards a remainder.

## Consequences

- ChatBreak, text, Pong, and EOF detection are delayed while admission is closed;
  an independent MQTT control path can carry urgent application notices.
- Intentional pauses suspend receive-liveness accounting. Resume starts a fresh
  `ping_timeout_ms` budget without pretending traffic arrived.
- Complete buffered Frames are processed before another read after resume, and a
  paused Packet's remaining frames are delivered before any later Packet;
  incomplete input returns to bounded blocking receive.
- A header-only START/ONE_SHOT reaches `on_audio` with `len == 0` without an
  admission check, so its server-side `timestamp_ms` can be latched for filtering.
- Interruption filtering is application-owned and time-based: `on_audio` compares
  the server timestamp against the interruption time from the MQTT notice or from
  ChatBreak's `user_data` (attr 111). The SDK exposes no receive state to mutate.
- `pal_t.sleep_ms` is mandatory, so custom PALs and consumers must rebuild.
