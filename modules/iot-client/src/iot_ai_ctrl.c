/*
 * AI control channel — MQTT protocol-9000 dispatch.
 *
 * Parses decrypted MQTT downlink envelopes with protocol==9000 and delivers
 * the inner event type + data JSON to the registered ai_ctrl_callback. Called
 * from mqtt_message_handler() before the DP dispatcher (protocol 5); the two
 * paths are mutually exclusive by protocol number.
 *
 * Envelope (PV23-decrypted plaintext):
 *   { "protocol": 9000, "t": <epoch>,
 *     "data": {
 *       "bizType": "EVENT",
 *       "bizId": "<uuid>",
 *       "data": {
 *         "type": "<event type, e.g. asrInterrupt>",
 *         "data": { ... event-specific JSON ... }
 *       } } } }
 */

#include "iot_client.h"
#include "iot_ai_ctrl.h"

#include <string.h>

#include "cJSON.h"

#define AI_CTRL_PROTO 9000

bool iot_ai_ctrl_dispatch(iot_client_t *client,
                          const uint8_t *bytes, size_t len)
{
    if (!client || !client->ai_ctrl_callback) return false;
    if (!bytes || len == 0) return false;

    cJSON *root = cJSON_ParseWithLength((const char *)bytes, len);
    if (!root) return false;

    cJSON *jproto = cJSON_GetObjectItem(root, "protocol");
    if (!cJSON_IsNumber(jproto) || jproto->valueint != AI_CTRL_PROTO) {
        cJSON_Delete(root);
        return false;
    }

    bool consumed = false;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    /* data.data.type and data.data.data */
    cJSON *inner = cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "data") : NULL;
    cJSON *jtype = cJSON_IsObject(inner) ? cJSON_GetObjectItem(inner, "type") : NULL;
    cJSON *jpayload = cJSON_IsObject(inner) ? cJSON_GetObjectItem(inner, "data") : NULL;

    if (cJSON_IsString(jtype)) {
        char *payload_str = NULL;
        if (jpayload) {
            payload_str = cJSON_PrintUnformatted(jpayload);
        }
        size_t plen = payload_str ? strlen(payload_str) : 0;
        client->ai_ctrl_callback(jtype->valuestring,
                                 payload_str ? payload_str : "",
                                 plen,
                                 client->ai_ctrl_user_data);
        if (payload_str) cJSON_free(payload_str);  /* cJSON_free = pal->free (hooks set in iot_init) */
        consumed = true;
    }

    cJSON_Delete(root);
    return consumed;
}

int iot_ai_ctrl_set_callback(iot_client_t *client,
                              ai_ctrl_callback_t cb, void *user_data)
{
    if (!client) return OPRT_INVALID_PARAMETER;
    client->ai_ctrl_callback  = cb;
    client->ai_ctrl_user_data = user_data;
    return OPRT_OK;
}
