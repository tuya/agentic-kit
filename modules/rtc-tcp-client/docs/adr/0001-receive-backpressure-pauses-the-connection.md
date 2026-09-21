# Receive backpressure pauses the Connection at Frame boundaries

The RTC TCP Client exposes an optional `on_flow_control` admission hook. When it
returns zero, the worker stops parsing complete buffered Frames and stops reading
the Connection, allowing the TCP receive window to close. This pauses all inbound
traffic rather than selecting only audio. The worker still runs Ping, shutdown,
and bounded admission checks through `pal_t.sleep_ms`.

We chose Frame-boundary admission because receive and reassembly buffers are
worker-owned borrowed storage. Deferring individual callback payloads would need
a second bounded queue and new lifetime rules. The application must still bound
its audio callback because one Audio Packet can emit multiple codec frames.

## Consequences

- ChatBreak, text, Pong, and EOF detection are delayed while admission is closed;
  an independent MQTT control path can carry urgent application notices.
- Intentional pauses suspend receive-liveness accounting. Resume starts a fresh
  `ping_timeout_ms` budget without pretending traffic arrived.
- Complete buffered Frames are processed before another read after resume;
  incomplete input returns to bounded blocking receive.
- `pal_t.sleep_ms` is mandatory, so custom PALs and consumers must rebuild.
