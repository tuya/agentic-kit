---
title: Calling an ATOP Interface Without a Named Wrapper (Generic Call)
sidebar_label: ATOP Generic Call
sidebar_position: 8
---

# Calling an ATOP Interface Without a Named Wrapper

The HTTP services between a device and the Tuya cloud are collectively called **ATOP interfaces**. Each is uniquely identified by two fields: its `api` name and `version`, for example, `tuya.device.upgrade.get` v4.4.

The SDK provides **named wrappers** for some of these interfaces (`iot_ota_*`, `iot_dp_*`, and others), returning typed structs. The cloud exposes far more callable ATOP interfaces than the SDK wraps. To prevent SDK scheduling from blocking application development, the SDK provides the **generic call** `iot_atop_call()`: supply an `api`, `version`, and JSON request body, and receive the `result` field as a JSON string.

Signing, AES-GCM request-body encryption, TLS, host resolution, and Envelope parsing all happen inside the SDK. **Device keys never leave the SDK.**

## First Check for a Named Wrapper {#先确认有没有具名接口}

Do not reimplement an interface through the generic call when it already has a named wrapper. Named wrappers additionally handle cloud response variants and have unit-test coverage:

| ATOP api | version | Named wrapper |
| --- | --- | --- |
| `tuya.device.upgrade.get` | 4.4 | `iot_ota_check_upgrade()` |
| `tuya.device.versions.update` | 4.1 | `iot_ota_report_version()` |
| `tuya.device.upgrade.status.update` | 4.1 | `iot_ota_report_status()` |
| `tuya.device.schema.newest.get` | 1.0 | Queried automatically inside the DP layer |
| `thing.ai.agent.token.get` | 1.0 | `iot_client_get_session_token()` |
| `tuya.device.qrcode.info.get` | 1.1 | `iot_get_qrcode_info()` |
| `thing.device.opensdk.active` | 2.0 | `iot_client_init_on_boarding()` |
| `tuya.device.meta.save` | 1.0 | Called internally during Activation |

## When to Request a Named Wrapper {#什么时候该要一个具名接口}

The generic call is an escape hatch, not the preferred option. If an interface meets **any** of the following criteria, it is worth adding to the SDK as a named wrapper; issues are welcome:

- **Reused across products** — two or more product lines need it. Otherwise, adding it to the SDK amounts to putting one product's application logic in the SDK.
- **Non-trivial protocol semantics** — it has a state machine, multi-step sequencing, or cloud response shapes that require tolerant parsing. Reimplementing this knowledge inevitably creates inconsistencies.
- **Must participate in SDK internal state** — it needs to change fields in `iot_client_t` or trigger SDK callbacks. The application layer cannot access internal state, so this can only be implemented inside the SDK.

When none of these criteria apply, keeping the interface in the application layer and invoking it through the generic call is an appropriate final design, not technical debt.

## API {#api}

```c
#include "iot_atop.h"

typedef struct {
    const char *api;      /* Example: "tuya.device.upgrade.get"; required */
    const char *version;  /* Example: "4.4"; required */
    const char *data;     /* Request body as a JSON object string; NULL or "" means "{}" */
} iot_atop_request_t;

typedef struct {
    char *result;            /* JSON text representing the result field; NULL if the cloud omitted it */
    char  error_code[48];    /* Cloud errorCode; "" on success */
    char  error_msg[128];    /* Cloud errorMsg; "" on success */
    int32_t server_time;     /* Server time t from the Envelope */
} iot_atop_response_t;

int  iot_atop_call(iot_client_t *client,
                   const iot_atop_request_t *request,
                   iot_atop_response_t *response);
void iot_atop_response_free(iot_client_t *client, iot_atop_response_t *response);
```

## Example {#示例}

```c
#include "iot_atop.h"

char body[192];
snprintf(body, sizeof(body),
         "{\"schemaId\":\"%s\",\"version\":\"\",\"t\":%u}",
         schema_id, (unsigned)time(NULL));

iot_atop_request_t  req  = { .api     = "tuya.device.schema.newest.get",
                              .version = "1.0",
                              .data    = body };
iot_atop_response_t resp = {0};

int rc = iot_atop_call(client, &req, &resp);
if (rc == OPRT_OK) {
    if (resp.result != NULL) {
        my_parse(resp.result);          /* result is JSON text; use any parser */
    }
} else if (rc == OPRT_ATOP_BUSINESS_ERROR) {
    /* The request reached the cloud but was rejected — error_code explains why */
    log_error("rejected: %s (%s)", resp.error_code, resp.error_msg);
} else {
    /* Transport-layer failure: DNS / TLS / HTTP / decryption */
    log_error("call failed: %d", rc);
}

iot_atop_response_free(client, &resp);   /* Call on every path, including failures */
```

## Return Values: Distinguishing Cloud Rejection from Connection Failure {#返回值区分云端拒绝和没连上}

This is the most important point when using the generic call. The SDK does not know the interface being invoked, so it cannot determine business success on your behalf. **The cloud's own errorCode is the only reliable indicator**:

| Return value | Meaning | Handling |
| --- | --- | --- |
| `OPRT_OK` | The cloud accepted the call | `result` contains the result JSON, or is NULL if the cloud omitted `result`, which is valid |
| `OPRT_ATOP_BUSINESS_ERROR` | The request reached the cloud but the cloud rejected it | Inspect `error_code` / `error_msg` and handle them according to the interface documentation |
| `OPRT_INVALID_PARAMETER` | The parameters are invalid, or `data` is not a JSON object | Rejected locally; no network request was sent |
| `OPRT_UNINITIALIZED` | The device does not yet have Activation credentials | Complete Activation first |
| Other | Transport-layer failure (DNS / TLS / HTTP / decryption) | May be retried |

`error_code[0] == '\0'` is equivalent to "business success."

## Limitations {#限制}

**Only activated devices are supported.** The generic call signs requests with `devid` + `secret_key`. Activation itself uses `uuid` + `authkey`, which is a separate path and must still go through `iot_client_init_on_boarding()`. Calling the generic call on a client without credentials returns `OPRT_UNINITIALIZED`.

**A single response cannot exceed 4096 bytes, including HTTP headers.** The status line, response headers, and encrypted response body share one fixed-size buffer (`RESPONSE_BUFFER_SIZE` in `http_client_interface.c`), with no segmented continuation reads. On overflow, the underlying coreHTTP returns `HTTPInsufficientMemory`, which the SDK collapses to `OPRT_COMMUNICATION_ERROR`—the same return value as a broken socket. Because coreHTTP's own explanatory logging is compiled out in this project (`HTTP_DO_NOT_USE_CUSTOM_CONFIG`), the only field symptom is a single "transport-layer failure," and retrying will not help. After accounting for response headers, base64 expansion, and Envelope fields, **the practical limit for decrypted JSON is approximately 2.8 KB**. This is ample for interfaces that return fixed-length fields, but interfaces returning lists or a DP Schema whose length grows with the product can easily exceed it. If you encounter such an interface, open an issue; the change that adds its named wrapper must also increase this constant.

**The request body is passed through unchanged.** The SDK does not rewrite it, so you must provide every field required by the interface—**including the `t` timestamp field that most ATOP interfaces require in the request body**. The SDK validates only that the body can be parsed as a JSON object. This turns a typo into an immediate `OPRT_INVALID_PARAMETER` rather than spending an HTTPS round trip to receive an ambiguous cloud rejection.

**`result` is a string, not a cJSON object.** This keeps cJSON out of the SDK's public ABI, avoids binding the application layer to the SDK's cJSON version, and makes memory ownership explicit: `iot_atop_response_free()` releases it.

**The device clock must be reasonably accurate.** The signature includes a timestamp, and the cloud rejects it when the device clock differs too much. `resp.server_time` is the time returned by the cloud, as a Unix timestamp in seconds, and can be used to correct the local clock.
