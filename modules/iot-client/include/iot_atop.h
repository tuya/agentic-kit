/**
 * @file iot_atop.h
 * @brief Generic ATOP call — reach a cloud interface the SDK does not wrap by name.
 *
 * The SDK wraps a handful of ATOP interfaces by name (activation, OTA, schema
 * upgrade, AI session token …). Everything else is reachable through this one
 * entry point: you supply the `api` name, its `version`, and the request body as
 * a JSON string; you get the `result` field back as a JSON string.
 *
 * Signing, AES-GCM body encryption, TLS, host resolution and envelope parsing
 * are all handled here — the device's secret key never leaves the SDK.
 *
 * ## When to use a named API instead
 *
 * Prefer an existing named API (`iot_ota_*`, `iot_dp_*`, …) whenever one covers
 * what you need: they return typed structs, tolerate the cloud's response
 * variations, and are covered by tests. Reach for `iot_atop_call()` when no
 * named API exists — and ask for one to be added when the interface is used by
 * more than one product, has non-trivial protocol semantics, or has to touch
 * SDK-internal state.
 *
 * ## Activated devices only
 *
 * The call signs with the device's `devid` + `secret_key`, so it works only
 * after activation. It returns `OPRT_UNINITIALIZED` on a client that has no
 * credentials yet. Activation itself uses a different credential pair and stays
 * behind `iot_client_init_on_boarding()`.
 *
 * ## Request bodies are passed through verbatim
 *
 * The body is not rewritten, so it must already carry whatever the interface
 * requires — including the `t` timestamp field that most ATOP interfaces expect
 * inside the body. It is validated only as far as "parses as a JSON object", to
 * turn a typo into an immediate error instead of an HTTPS round trip.
 *
 * ## Example
 *
 * ```c
 * char body[128];
 * snprintf(body, sizeof(body),
 *          "{\"schemaId\":\"%s\",\"version\":\"\",\"t\":%u}",
 *          schema_id, (unsigned)time(NULL));
 *
 * iot_atop_request_t  req  = { .api = "tuya.device.schema.newest.get",
 *                              .version = "1.0",
 *                              .data = body };
 * iot_atop_response_t resp = {0};
 *
 * int rc = iot_atop_call(client, &req, &resp);
 * if (rc == OPRT_ATOP_BUSINESS_ERROR) {
 *     // reached the cloud, cloud said no -- resp.error_code says why
 *     log_error("rejected: %s (%s)", resp.error_code, resp.error_msg);
 * } else if (rc == OPRT_OK) {
 *     // resp.result is the "result" field as JSON, or NULL if it was null
 *     my_parse(resp.result);
 * }
 * iot_atop_response_free(client, &resp);
 * ```
 */

#ifndef __IOT_ATOP_H__
#define __IOT_ATOP_H__

#include <stdint.h>

#include "iot_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Buffer sizes for the cloud's error strings (see iot_atop_response_t). */
#define IOT_ATOP_ERROR_CODE_LEN  48
#define IOT_ATOP_ERROR_MSG_LEN   128

/**
 * @brief What to call.
 *
 * `api` and `version` together identify an ATOP interface, exactly as they
 * appear in the cloud's interface documentation.
 */
typedef struct {
    const char *api;      /**< e.g. "tuya.device.upgrade.get" (required) */
    const char *version;  /**< e.g. "4.4" (required) */
    const char *data;     /**< request body, a JSON object string; NULL/"" = "{}" */
} iot_atop_request_t;

/**
 * @brief What came back.
 *
 * `error_code` is the discriminator: it is `""` on success and carries the
 * cloud's own code (e.g. "ILLEGAL_PARAM") when the call was rejected. Free with
 * iot_atop_response_free() regardless of the return code.
 */
typedef struct {
    char *result;                              /**< "result" field as JSON; NULL if the cloud sent none */
    char  error_code[IOT_ATOP_ERROR_CODE_LEN]; /**< cloud errorCode; "" on success */
    char  error_msg[IOT_ATOP_ERROR_MSG_LEN];   /**< cloud errorMsg;  "" on success */
    int32_t server_time;                       /**< server time from the envelope ("t"), seconds since epoch */
} iot_atop_response_t;

/**
 * @brief Call an ATOP interface by name.
 *
 * The return code separates transport from business outcome:
 * - `OPRT_OK` — the cloud accepted the call. `response->result` holds the
 *   result JSON, or is NULL when the cloud returned no result.
 * - `OPRT_ATOP_BUSINESS_ERROR` — the call reached the cloud and was rejected.
 *   `response->error_code` / `error_msg` say why.
 * - `OPRT_INVALID_PARAMETER` — bad arguments, or `data` is not a JSON object.
 * - `OPRT_UNINITIALIZED` — the client has no device credentials yet.
 * - anything else — transport-level failure (DNS, TLS, HTTP, decrypt).
 *
 * `response` is zeroed on entry, so it is safe to pass the same struct to
 * repeated calls as long as each is followed by iot_atop_response_free().
 *
 * @param[in]  client   Activated IoT client (supplies credentials + endpoint)
 * @param[in]  request  Interface to call and the body to send
 * @param[out] response Result and/or the cloud's error strings
 * @return see above
 */
IOT_API int iot_atop_call(iot_client_t *client,
                          const iot_atop_request_t *request,
                          iot_atop_response_t *response);

/**
 * @brief Release an iot_atop_response_t and zero it.
 *
 * Safe on an all-zero struct and safe to call twice. Call it on every path,
 * including after a failed iot_atop_call().
 */
IOT_API void iot_atop_response_free(iot_client_t *client,
                                    iot_atop_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* __IOT_ATOP_H__ */
