/**
 * @file iot_atop.c
 * @brief Public generic ATOP call — thin, deliberately untyped layer over
 *        atop_base_request().
 *
 * Everything hard (URL signing, AES-GCM body encryption, TLS, envelope parsing)
 * already lives in atop_base.c. This file exists to make that capability a
 * supported public API rather than a reachable internal one: it sources the
 * credentials and endpoint from iot_client_t so the caller never handles the
 * secret key, and it converts the internal cJSON result into a plain JSON
 * string so cJSON stays out of the public ABI.
 *
 * Follows the same host-resolution pattern as iot_ota.c.
 */

#include "iot_atop.h"

#include "atop_base.h"
#include "cJSON.h"
#include "iot_internal.h"   /* IOT_DEFAULT_PORT */
#include "iot_dp_internal.h"       /* iot_client_resolve_atop_host */

#include <stdio.h>
#include <string.h>
#include <time.h>

/* The public buffers mirror the internal envelope buffers. Static-assert rather
 * than re-deriving one from the other, so a future widening of either side
 * fails the build instead of silently truncating the cloud's error code. */
_Static_assert(IOT_ATOP_ERROR_CODE_LEN >= ATOP_ERROR_CODE_LEN,
               "public error_code buffer must not truncate the envelope's");
_Static_assert(IOT_ATOP_ERROR_MSG_LEN >= ATOP_ERROR_MSG_LEN,
               "public error_msg buffer must not truncate the envelope's");

/* A generic call forwards the caller's body, but every ATOP request body must
 * carry the request timestamp: each typed wrapper in atop.c ends its body with
 * "t", and the cloud binds the URL `t` to the body `t` -- a body without one is
 * rejected on the sign path. So the caller's object is parsed, its `t` forced to
 * the SDK's own timestamp (the same one that signs the URL), and re-serialized.
 * A malformed non-object body is still caught here, before spending an HTTPS
 * round trip, and the cloud's rejection reason for it is far less specific than
 * "your JSON does not parse". */
static int atop_body_prepare(const char *data, uint32_t timestamp, char **out)
{
    *out = NULL;
    /* require_null_terminated: plain cJSON_Parse() stops at the first complete
     * value and would wave through "{}garbage" -- exactly the snprintf-slip
     * class this check exists to catch locally. */
    cJSON *root = cJSON_ParseWithOpts(data, NULL, 1);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return OPRT_INVALID_PARAMETER;
    }
    /* Overwrite any caller-supplied t: the URL signs the SDK's timestamp, so a
     * stale caller value would sign one t and send another. */
    cJSON_DeleteItemFromObject(root, "t");
    cJSON_AddNumberToObject(root, "t", (double)timestamp);
    *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return (*out != NULL) ? OPRT_OK : OPRT_MALLOC_FAILED;
}

int iot_atop_call(iot_client_t *client,
                  const iot_atop_request_t *request,
                  iot_atop_response_t *response)
{
    if (response == NULL) {
        return OPRT_INVALID_PARAMETER;
    }
    /* Zero before any other validation: the header promises "zeroed on entry"
     * and that iot_atop_response_free() is safe on every path -- an early
     * return that skipped this would hand back stale (or stack-garbage)
     * error_code/result for the caller to act on or free. */
    memset(response, 0, sizeof(*response));

    if (client == NULL || client->pal == NULL || request == NULL) {
        return OPRT_INVALID_PARAMETER;
    }
    if (request->api == NULL || request->api[0] == '\0' ||
        request->version == NULL || request->version[0] == '\0') {
        IOT_LOGE("iot_atop_call: api and version are required");
        return OPRT_INVALID_PARAMETER;
    }

    /* Activated devices only: this path signs with devid + secret_key.
     * Activation uses uuid + authkey and stays behind on-boarding. */
    if (client->devid[0] == '\0' || client->secret_key[0] == '\0') {
        IOT_LOGE("iot_atop_call: client has no device credentials yet");
        return OPRT_UNINITIALIZED;
    }

    const char *data = (request->data != NULL && request->data[0] != '\0')
                           ? request->data
                           : "{}";
    uint32_t timestamp = (uint32_t)time(NULL);

    char *body = NULL;
    int rt = atop_body_prepare(data, timestamp, &body);
    if (rt != OPRT_OK) {
        IOT_LOGE("iot_atop_call: %s",
                 rt == OPRT_INVALID_PARAMETER ? "data is not a JSON object"
                                              : "failed to prepare request body");
        return rt;
    }

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, host, sizeof(host), &port);

    atop_base_request_t atop_request = {
        .path      = "/d.json",
        .key       = client->secret_key,
        .api       = request->api,
        .version   = request->version,
        .devid     = client->devid,
        .timestamp = timestamp,
        .data      = body,
        .datalen   = strlen(body),
        .host      = host[0] ? host : NULL,
        .port      = port,
        .cacert    = client->cacert,
        .cert_bundle_attach = client->cert_bundle_attach,
    };

    atop_base_response_t atop_response = {0};
    rt = atop_base_request(client->pal, &atop_request, &atop_response);
    /* The body is encrypted into atop_base's own buffer inside the call, so it
     * is ours to release as soon as the request returns. */
    client->pal->free(body);

    /* Carry the cloud's verdict back on every path. For a generic call this is
     * the whole point: the caller knows what its interface can reject on, and
     * the SDK does not. */
    snprintf(response->error_code, sizeof(response->error_code), "%s",
             atop_response.error_code);
    snprintf(response->error_msg, sizeof(response->error_msg), "%s",
             atop_response.error_msg);
    response->server_time = atop_response.t;

    if (rt != OPRT_OK) {
        IOT_LOGE("iot_atop_call(%s v%s) failed: %d", request->api,
                  request->version, rt);
        atop_base_response_free(client->pal, &atop_response);
        return rt;
    }

    /* An absent result and a JSON null result both mean "nothing to report":
     * response->result stays NULL and the caller sees OPRT_OK. Without the
     * IsNull check a {"result":null} envelope would come back as the
     * four-character string "null", which the header explicitly rules out. */
    if (atop_response.result != NULL && !cJSON_IsNull(atop_response.result)) {
        /* The printed string is already pal-owned -- cJSON's hooks are the pal
         * allocator (cJSON_InitHooks in iot_init), and no successful response
         * can precede iot_init(): the AES-GCM nonce needs rng_bytes(), which
         * fails closed until rng_init() runs inside that same function. Same
         * reasoning as iot_dp_dump_json (iot_dp.c). */
        response->result = cJSON_PrintUnformatted(atop_response.result);
        if (response->result == NULL) {
            atop_base_response_free(client->pal, &atop_response);
            return OPRT_MALLOC_FAILED;
        }
    }

    atop_base_response_free(client->pal, &atop_response);
    return OPRT_OK;
}

void iot_atop_response_free(iot_client_t *client, iot_atop_response_t *response)
{
    if (client == NULL || client->pal == NULL || response == NULL) {
        return;
    }
    if (response->result != NULL) {
        client->pal->free(response->result);
    }
    memset(response, 0, sizeof(*response));
}
