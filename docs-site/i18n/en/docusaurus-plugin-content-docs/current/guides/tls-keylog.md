---
title: Decrypting TLS Captures with Wireshark (TLS key log)
sidebar_label: TLS Capture Decryption
sidebar_position: 9
---

# Decrypting TLS Captures with Wireshark

All traffic between the SDK and the cloud (IoT-DNS, ATOP HTTPS, MQTT, RTC/TAI) runs over TLS, so a packet capture shows only ciphertext. Troubleshooting problems like "the cloud says my field is wrong" or "this downlink frame won't parse" requires seeing the plaintext application-layer data.

The SDK provides a TLS key log: during the handshake it exports the session secrets in the NSS `SSLKEYLOGFILE` format; once Wireshark reads that file, it can decrypt the same capture. Compared with placing a TLS proxy in the middle, this changes neither the device's connection target nor its certificates.

:::danger What gets exported IS the session key itself

Every line in the key log file is one connection's session secret. Anyone who obtains it can decrypt all of that device's traffic for the period, including the MQTT password, ATOP signatures, and AI session tokens.

- Enable it only while troubleshooting, and only with a test account / test device;
- write the file somewhere that is neither packaged into firmware nor uploaded with logs;
- delete the file and turn the switch off once you are done.

It is off by default: if the application never calls the two functions below, the SDK exports nothing. But it is a **runtime** switch, not a compile-time one — both functions exist in every build. In production firmware the only visible sign that "the debug switch shipped enabled" is the `[tls] key logging ENABLED` warning log below.
:::

## Enabling It {#开启}

Both functions are declared in `common/tls.h`:

```c
#include "tls.h"

/* Option 1: write to a file directly (compiled in on POSIX and ESP-IDF by
   default; other targets need -DTLS_KEYLOG_FILE_SINK=1) */
int  tls_keylog_open_file(const char *path);
void tls_keylog_close_file(void);

/* Option 2: receive each line yourself (UART, log service, ring buffer, ...) */
typedef void (*tls_keylog_fn)(void *ctx, const char *line);
void tls_set_keylog_handler(tls_keylog_fn fn, void *ctx);
```

### Option 1: Write to a File {#方式一写文件}

```c
#include "tls.h"

int main(void)
{
    /* Call before the first TLS connection — iot_client_init() connects
       internally, so this must come before it. Check the return value: if the
       file cannot be opened, not a single line gets exported. */
    if (tls_keylog_open_file("/var/tmp/tuya-debug/keylog.txt") != TLS_OK) {
        fprintf(stderr, "key log disabled\n");   /* reason is in the [tls] log */
    }

    iot_client_t *client = iot_client_init(&cfg);
    /* ... run normally while capturing with tcpdump / Wireshark ... */

    iot_client_deinit(client);
    tls_keylog_close_file();
    return 0;
}
```

Do not use a fixed file name under a world-writable directory such as `/tmp`: the file holds session secrets, and someone can pre-place a same-named file or a symlink and wait. Use a path in your own directory.

On POSIX the file is created with `0600` permissions, symlinks are not followed, and each line is `fsync`ed as soon as it is written, so a device reboot mid-capture does not make the already-captured packets undecryptable. Other platforms (e.g. the ESP-IDF VFS) only guarantee the write bypasses stdio buffering; when it actually lands depends on how that filesystem treats unflushed writes. A failed write logs one `LOG_ERROR` (only once); subsequent lines are dropped.

### Option 2: Custom Sink {#方式二自定义-sink}

Targets without a filesystem (e.g. a UART-only MCU) use this: print each line to the serial console and save it into a file on the PC side.

```c
static void keylog_to_uart(void *ctx, const char *line)
{
    (void)ctx;
    /* line is NUL-terminated and carries its own newline; print as-is */
    uart_write_string(line);
}

tls_set_keylog_handler(keylog_to_uart, NULL);
```

`tls_set_keylog_handler(NULL, NULL)` turns export off. If the file sink is open at that moment, it is closed first. As with every sink change, call it while no `tls_connect()` is in progress.

## Scope and Timing {#作用范围与时机}

- **Process-wide; enabling once covers every connection**: MQTT, ATOP HTTPS, IoT-DNS, and the RTC/TAI channel all export — no per-connection configuration is needed.
- **Enable before the first `tls_connect()`, from a single thread.** `tls_connect()` reads the switch once while connecting: already-established connections are unaffected; only connections created afterwards export. The SDK has no internal lock — swapping the sink while another thread is mid-handshake is undefined behavior, not "one line late".
- The callback fires **on the thread that calls `tls_connect()`**: for iot-client that is the thread calling `iot_client_init()` / `iot_client_process()`; for RTC/TAI it is the thread calling `tai_connect()` (`tai_connect()` completes the handshake synchronously on the caller's thread before starting the receive thread). If both may connect at the same time, a custom sink must be **thread-safe** — write each whole line atomically (e.g. under a mutex), otherwise two lines interleave and Wireshark recognizes neither.
- Exporting requires mbedTLS 3.x (the repo ships 3.6). On 2.x the sink installs but produces no output; the log says so.
- Enabling logs one `LOG_WARN`:

  ```
  [tls] key logging ENABLED -- session secrets are being exported to /var/tmp/tuya-debug/keylog.txt; do not use in production
  ```

  Seeing this line in production logs means a firmware shipped with the debug switch enabled.

## What Gets Exported {#导出的内容}

The line format depends on the negotiated TLS version:

| Channel | TLS version | Exported lines |
|---------|-------------|----------------|
| iot-client (MQTT / ATOP / DNS) | Fixed TLS 1.2 | One `CLIENT_RANDOM` line per connection |
| RTC/TAI | Server-negotiated, possibly TLS 1.3 | Multiple lines such as `CLIENT_HANDSHAKE_TRAFFIC_SECRET` / `SERVER_HANDSHAKE_TRAFFIC_SECRET` / `CLIENT_TRAFFIC_SECRET_0` / `SERVER_TRAFFIC_SECRET_0` |

Example (TLS 1.2, keys rewritten):

```
CLIENT_RANDOM 5f2e... (64 hex characters) 9a41... (96 hex characters)
```

## Using It in Wireshark {#在-wireshark-中使用}

1. Capture. When the device and the PC are the same machine:

   ```sh
   sudo tcpdump -i any -w tuya.pcap 'tcp port 8883 or tcp port 443'
   ```

   With standalone hardware, capture at a mirror port / tap between the device and the router.

2. Point Wireshark at the key log file: **Preferences → Protocols → TLS → `(Pre)-Master-Secret log filename`**, and select the file just written. Command-line equivalent:

   ```sh
   tshark -r tuya.pcap -o tls.keylog_file:/var/tmp/tuya-debug/keylog.txt -Y mqtt
   ```

3. Packets that previously showed as `Application Data` now decode to MQTT / HTTP plaintext. Filter directly on `mqtt` or `http`.

The capture and the key log must come from **the same run**: secrets are per-connection, and a different run will not match.

## When It Won't Decrypt {#解不出来时}

| Symptom | Cause |
|---------|-------|
| Key log file missing or empty | `tls_keylog_open_file()` returned an error (path not writable, filesystem not mounted, file sink not compiled in on this platform) — see the reason in the `[tls] cannot open key log file` log; or the switch was set after the first connection (`iot_client_init()` already connected inside); or no TLS connection was ever established — see the handshake-log notes in [TLS Certificate Verification](./tls-cert-verification.md#查看日志) |
| File has content but stops partway | A write failed; the log has one `[tls] key log write failed` line (disk full, filesystem turned read-only) |
| File has content but Wireshark still shows ciphertext | The capture and the key log are from different runs; or the capture missed the handshake (the Client Hello must be in the capture — Wireshark matches keys by client random) |
| Only some connections decrypt | That connection's handshake happened before enabling, or the capture started mid-way |
| MQTT decodes to plaintext but the payload is still gibberish | Normal: above the MQTT payload sits another AES-GCM layer (encrypted with the first 16 bytes of `local_key`) — that is the SDK's application-layer encryption, not TLS |

## See Also {#相关}

- [TLS Certificate Verification](./tls-cert-verification.md) — what to configure for production; unrelated to this page's debug switch.
