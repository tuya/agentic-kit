# MQTT AI control remains application-composed

The IoT Client consumes authenticated MQTT protocol-9000 AI control notices and
passes their event type and data to an application callback. It does not depend
on the RTC TCP Client, inject TAI Events, stop playback, or send an automatic
acknowledgement. The application keeps one owner for MQTT process/publish and
combines this callback with RTC receive callbacks in its own synchronized state.

We chose a separate MQTT control path because intentional RTC receive
backpressure stalls every Frame on that Connection, including a later ChatBreak.
Keeping the modules independent preserves their existing threading models and
lets an interrupt reach the application without waiting behind media bytes.

## Consequences

- The application must keep MQTT connected and call `iot_client_process()` while
  a TAI Session is active.
- Callback data is borrowed and the callback must return promptly.
- Event correlation, duplicate suppression, queue flushing, and rejection of
  stale media are application responsibilities.
- A server notice does not imply calling `tai_chat_break()` or ending a
  server-VAD uplink.
