# PAL separates UTC timestamps from monotonic time

Before this change, `pal_t.time_ms` provided monotonic milliseconds for TLS,
MQTT and RTC Connection deadlines, while cloud requests bypassed the PAL and
read libc `time(NULL)`. A FreeRTOS port could provide ticks without a UTC source
visible to signing; RTC media timestamps also had a platform-local epoch.

We retain `pal_t.time_ms` for elapsed time, Connection deadlines, Keepalive and
drain budgets, and add mandatory `pal_t.wal_clock_time` for Unix UTC milliseconds
without a timezone offset. UTC may move forwards or backwards; its corrections
must never affect the monotonic clock. Zero denotes unavailable UTC. The
application synchronizes UTC before cloud requests or opening an RTC Connection;
the SDK performs no clock synchronization or background work.

Cloud signing and DP reports call `pal->wal_clock_time()` directly and convert
UTC milliseconds to `uint32_t` Unix seconds at the protocol call sites. The PAL
exposes clock sources without protocol-specific conversion helpers.
RTC media and Ping payloads use `wal_clock_time` in
milliseconds; receive liveness uses `time_ms`. POSIX uses `CLOCK_REALTIME` for
UTC and `CLOCK_MONOTONIC` for elapsed time. The lwIP/ESP-IDF PAL reads UTC through
`gettimeofday`, with the application maintaining the C library clock. Both
implementations treat non-positive Unix seconds or a failed UTC read as unavailable.
FreeRTOS clock implementation stays private to `pal_freertos.c`. Other ports own
their UTC source, concurrent reads/updates and transport-delay compensation.

The FreeRTOS monotonic implementation reads `TimeOut_t` through
`vTaskSetTimeOutState()`. Upstream [V10.4.3](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/9c048e0c71ee43630394981a86f5265bc57131e4/tasks.c#L3239)
and [V11.1.0](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/dbf70559b27d39c1fdb68dfb9a32140b6a6777a0/tasks.c#L5012)
snapshot `xNumOfOverflows` and `xTickCount` inside a critical section. Using this
timeout bookkeeping as an extended clock depends on these implementations and
the `TimeOut_t` fields; it is not a stable extended-clock contract. Integrators
must recheck vendor forks and kernel upgrades, including critical-section behavior
on their port. For 16/32-bit ticks, the overflow count must remain representable
and `UBaseType_t` must be at least 32 bits. Native 64-bit ticks use their own count.

The checked-in header probe fixes both upstream commit IDs and compiles the
actual PAL with V10.4.3 16/32-bit ticks and V11.1.0 16/32/64-bit ticks:

```sh
cmake --build build --target check_freertos_pal
# Or independently of CMake:
python3 tools/check_freertos_pal.py --cc cc
```

This downloads official headers and runs five C99 syntax checks using a type-only
host port. Offline use accepts pinned git checkouts through `--kernels-dir`.
GitHub and GitLab CI run the same check. It verifies header/type compatibility;
it does not execute the real kernel or validate its critical sections. Host
behavior tests separately compile the actual FreeRTOS PAL against a modeled
kernel snapshot, and the actual POSIX PAL against injected clock readings.

## Consequences

- The PAL API and ABI change: integrators must rebuild and provide both callbacks.
  Existing tick/uptime callbacks remain in `time_ms`; `wal_clock_time` needs
  synchronized UTC. `pal_is_valid` rejects either missing callback.
- Tick-based readings retain kernel tick resolution. Tickless idle counts only
  the sleep that the port compensates in kernel ticks. A kernel restart establishes
  a new monotonic epoch, and the application restores UTC if it was not retained.
- Synchronizing a custom PAL UTC callback alone does not set libc/mbedTLS's
  certificate validation clock. A port must connect those to the same UTC source
  when its TLS configuration enables certificate-time checks.
- Regression coverage checks tick widths/frequencies/wraps, unavailable/failed
  UTC reads, UTC corrections and Connection liveness. An independent ATOP peer
  verifies the URL MD5 signature before echoing the UTC timestamp, and rejects
  missing signatures, invalid digests and timestamp tampering. Host shims do not
  replace device integration checks.
