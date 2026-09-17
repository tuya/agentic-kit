---
title: TLS Certificate Verification
sidebar_label: TLS Certificate Verification
sidebar_position: 7
---

# TLS Certificate Verification

All agentic-kit cloud communication (IoT-DNS, ATOP HTTPS, and MQTT) uses TLS by default. IoT-DNS and ATOP HTTPS use port 443, while MQTT over TLS (mqtts, default `mqtts://a6.tuyacn.com:8883`) uses port 8883. If no certificate verification information is provided in the configuration, the connection is still encrypted, but **the server certificate is not verified**, creating a man-in-the-middle (MITM) risk.

The SDK provides two ways to enable server certificate verification:

| Method | Use case | How it works |
|--------|----------|--------------|
| `.cacert` (PEM string) | POSIX (Linux/macOS), or devices with enough RAM for a complete PEM | Pass the CA root certificate as PEM text; the SDK parses it and uses it for mbedTLS certificate-chain verification |
| `.cert_bundle_attach` (platform certificate-bundle callback) | RTOS platforms such as ESP-IDF, where RAM is constrained or certificates are managed as compiled binary data | Pass the platform SDK's certificate-bundle attachment function (such as `esp_crt_bundle_attach`) to operate directly on mbedTLS's `ssl_config` |

Both may be unset for backward compatibility, which falls back to no verification, but this is not recommended in production.

## Verification Precedence {#验证优先级}

When both fields are provided, `.cacert` takes precedence. The underlying `tls_connect()` logic is:

```
1. Is cacert non-empty?             → Parse the PEM from cacert; authmode = VERIFY_REQUIRED
2. Is cert_bundle_attach non-NULL?  → Invoke the callback to attach the platform certificate bundle;
                                      authmode = VERIFY_REQUIRED
3. Are both empty?                  → authmode = cfg->verify
```

In step 3, each upper-level module sets `cfg->verify`; it is not exposed through iot-client's public configuration. **iot-client always passes `TLS_VERIFY_NONE`** (when both fields are empty, the connection is encrypted but not verified, and a warning is logged), while RTC/TAI channels pass `TLS_VERIFY_OPTIONAL`. Therefore, for iot-client users, "both empty = no verification" is accurate. This is why production configurations must set at least one of `.cacert` and `.cert_bundle_attach`.

## Affected Connections {#涉及的连接}

After configuration, certificate verification applies to **all** of these connections:

- IoT-DNS queries (`h1.iot-dns.com:443`)
- ATOP HTTPS requests (device metadata, session token, OTA queries, and others)
- MQTT connections (when `mqtt_disable_tls = false`)

There is no need to configure each connection separately. Configure it once on `iot_client_config_t` or `iot_on_boarding_config_t`.

---

## Method 1: `.cacert` (PEM String) {#方式一cacertpem-字符串}

Use this on POSIX platforms or where sufficient memory is available to embed PEM text directly in the code.

### Usage {#用法}

```c
/* Root CA certificate PEM — may point to a static string or heap-allocated PEM text,
   but it must remain valid for the client's entire lifetime (the SDK does not copy it). */
static const char *root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDx...(omitted)...\n"
    "-----END CERTIFICATE-----\n";

iot_client_config_t cfg = {
    .devid      = "...",
    .secret_key = "...",
    .local_key  = "...",
    .region     = AY,
    .env        = PROD,
    .cacert     = root_ca,          /* ← Set the CA certificate */
};
iot_client_t *client = iot_client_init(&cfg);
```

### Obtaining a CA Certificate at Runtime {#运行时获取-ca-证书}

If you do not want to hard-code a PEM in the firmware, you can query IoT-DNS at runtime for the CA certificate of **a specific target endpoint**. The function requires the target hostname and port (`host` must not be NULL; otherwise it returns `OPRT_INVALID_PARAMETER`):

```c
int iot_get_ca_certificate(iot_client_t *client, const char *host,
                           uint16_t port, char *ca_certificate, size_t ca_certificate_len);
```

```c
/* Query the CA certificate for the target endpoint (for example, port 443 for an
   ATOP/HTTPS host or 8883 for MQTT). The certificate is written into a caller-provided
   buffer. A single CA PEM is usually 1–2 KB, so 4096 bytes is sufficient;
   OPRT_INVALID_RESULT is returned when the buffer is too small. */
static char ca_cert[4096];
int rc = iot_get_ca_certificate(client, "a1.tuyacn.com", 443, ca_cert, sizeof(ca_cert));
if (rc == OPRT_OK) {
    /* ... Use / persist ca_cert ...
       If assigned to client->cacert for long-term use, the buffer must outlive the client
       (for example, make it static). */
}
```

> **Bootstrap catch-22:** `iot_get_ca_certificate()` must first establish a TLS connection to IoT-DNS. If `client->cacert` is still empty, that query runs without verification, creating a bootstrap-stage MITM risk. Likewise, before `iot_client_init()` returns, it has already completed the **initial DNS/ATOP connections** (and, because automatic connection is enabled by default, the initial MQTT connection). Therefore, setting `client->cacert` after init affects only later requests and cannot retroactively protect connections made during init.
>
> Runtime CA retrieval is therefore better suited to **retrieving and persisting a CA** so it can be supplied through `cfg.cacert` **before** `iot_client_init()` on the next boot, enabling verification from the first connection. For environments with strict security requirements, hard-code the root CA or use `.cert_bundle_attach` instead.

### Memory Considerations {#内存注意事项}

- A complete Mozilla CA bundle is approximately 200 KB and may be too large for RAM-constrained MCUs.
- A single Tuya cloud CA certificate is approximately 1–2 KB and is generally acceptable.
- The string referenced by `.cacert` is not copied. The caller must keep it valid until after `iot_client_deinit()`.

---

## Method 2: `.cert_bundle_attach` (Platform Certificate-Bundle Callback) {#方式二cert_bundle_attach平台证书包回调}

Use this on RTOS platforms such as ESP-IDF. The platform manages the certificate bundle as compiled binary data stored in a flash partition or firmware image, so PEM text does not need to be held in RAM.

### How It Works {#原理}

ESP-IDF provides `esp_crt_bundle_attach()`, which attaches a precompiled CA certificate bundle containing major public root CAs directly to mbedTLS's `ssl_config`. The SDK invokes this callback before the TLS handshake, and mbedTLS uses the bundle's CAs to verify the server certificate during the handshake.

### ESP-IDF Usage {#esp-idf-用法}

```c
#include "esp_crt_bundle.h"
#include "iot_client.h"

iot_client_config_t cfg = {
    .devid      = "...",
    .secret_key = "...",
    .local_key  = "...",
    .region     = AY,
    .env        = PROD,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};
iot_client_t *client = iot_client_init(&cfg);
```

The same applies during Activation (on-boarding):

```c
iot_on_boarding_config_t ob_cfg = {
    .uuid       = "...",
    .authkey    = "...",
    .product_key = "...",
    .env        = PROD,
    .mqtt_disable_tls = false,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
};
iot_client_t *client = iot_client_init_on_boarding(&ob_cfg);
```

### ESP-IDF sdkconfig Configuration {#esp-idf-sdkconfig-配置}

Ensure that the certificate bundle is enabled in `sdkconfig`:

```ini
# Enable the mbedTLS certificate bundle (includes major public root CAs by default)
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

### Type-Cast Note {#类型转换说明}

The signature of `esp_crt_bundle_attach` is `esp_err_t (*)(void *conf)`, while the SDK defines `tls_cert_bundle_attach_fn` as `void (*)(void *ssl_config)`. Because the signatures do not match exactly, an explicit `(tls_cert_bundle_attach_fn)` cast is required. This is safe: the callback's semantics are to attach certificates to `mbedTLS_ssl_config`, and ESP-IDF's implementation is compatible with mbedTLS's `mbedtls_ssl_conf_verify` / CA-chain operations.

### RTC/TAI Connections (AI Conversation Channel) {#rtctai-连接ai-对话通道}

RTC TCP Client also supports `cert_bundle_attach`, configured in the same way as iot-client:

```c
tai_config_t tai_cfg = {
    .host              = cp.host,
    .port              = cp.port,
    .cert_bundle_attach = (tls_cert_bundle_attach_fn)esp_crt_bundle_attach,
    /* ... Other fields ... */
};
```

This lets both iot-client and RTC/TAI use the same certificate bundle, so all cloud connections are verified.

### Other RTOS Platforms {#其他-rtos-平台}

If the target platform is not ESP-IDF but provides a similar certificate-bundle mechanism, implement a callback with a signature matching `tls_cert_bundle_attach_fn`. Inside the callback, attach the platform certificate bundle to the supplied `mbedTLS_ssl_config *`.

---

## Affected APIs {#哪些-api-受影响}

The configuration structs for these public APIs all support `.cacert` and `.cert_bundle_attach`:

| API | Configuration struct | Description |
|-----|----------------------|-------------|
| `iot_client_init` | `iot_client_config_t` | Initialization for an activated device (DNS + ATOP + MQTT) |
| `iot_client_init_on_boarding` | `iot_on_boarding_config_t` | Activation by App QR-code scan |
| `iot_client_init_on_boarding_with_token` | `iot_on_boarding_config_t` | Token-based Activation |
| `iot_get_qrcode_info` | `iot_qrcode_request_t` | Obtains the provisioning QR-code URL (standalone API; no client required) |

After configuration, every TLS connection initiated within these APIs uses certificate verification.

---

## Confirming That Verification Is Active {#如何确认验证是否生效}

### Check the Logs {#查看日志}

The SDK logs during the TLS handshake. When certificate verification is enabled:

- **`cacert` is active**: no warning is logged; after a successful TLS handshake, `[tls] connected ...` is logged
- **`cert_bundle_attach` is active**: likewise, there is no "verification disabled" warning
- **Neither is configured**: this warning is logged at `LOG_WARN` level:
  ```
  [tls] peer verification disabled (no CA certificate)
  ```

If this warning appears, certificate verification is not enabled for that connection.

### Verification Failure Errors {#验证失败的错误}

When certificate verification is enabled but the server certificate is not trusted, the TLS handshake fails. MQTT / ATOP HTTPS paths return `OPRT_TLS_HANDSHAKE_FAILED` (-7), while **the IoT-DNS query path normalizes TLS failures to `OPRT_COMMUNICATION_ERROR` (-1)**. Therefore, when troubleshooting failures during DNS/CA retrieval, do not match only -7. Common causes include:

- The server certificate chain cannot be traced to the supplied CA
- The certificate bundle does not contain the Tuya cloud root CA
- The system time is incorrect (certificates have Not Before / Not After validity windows)

---

## Best Practices {#最佳实践}

1. **Enable certificate verification in production.** Configure at least one of `.cacert` and `.cert_bundle_attach`.
2. **Prefer `.cert_bundle_attach` on ESP-IDF.** The certificate bundle is stored in flash and supports major public CAs without consuming RAM for a PEM.
3. **Use `.cacert` on POSIX platforms.** Obtain it dynamically from IoT-DNS or hard-code the root CA PEM.
4. **Do not leave both fields empty in production.** The connection is encrypted but unverified, creating an MITM risk.
5. **Keep the system clock accurate.** Certificates have validity periods, and excessive clock skew causes verification to fail. MCU platforms should synchronize time through NTP after connecting to the network.
