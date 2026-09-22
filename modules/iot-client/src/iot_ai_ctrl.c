#include "iot_ai_ctrl.h"

#include <string.h>

#include "cJSON.h"

#define IOT_PROTO_AI_CONTROL 9000

bool iot_ai_ctrl_dispatch(iot_client_t *client,
                          const uint8_t *bytes, size_t len)
{
    if (!client || !client->ai_ctrl_callback || !bytes || len == 0) {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)bytes, len);
    if (!root) return false;

    cJSON *protocol = cJSON_GetObjectItem(root, "protocol");
    if (!cJSON_IsNumber(protocol) ||
        protocol->valuedouble != (double)IOT_PROTO_AI_CONTROL) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *inner = cJSON_IsObject(data)
                     ? cJSON_GetObjectItem(data, "data") : NULL;
    cJSON *type = cJSON_IsObject(inner)
                    ? cJSON_GetObjectItem(inner, "type") : NULL;
    cJSON *payload = cJSON_IsObject(inner)
                       ? cJSON_GetObjectItem(inner, "data") : NULL;
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return false;
    }

    char *payload_json = NULL;
    const char *payload_bytes = "";
    size_t payload_len = 0;
    if (payload) {
        payload_json = cJSON_PrintUnformatted(payload);
        if (!payload_json) {
            /* Do not turn a scoped notice into an unscoped empty payload under
             * allocation pressure. Leave the authenticated message unconsumed
             * so the application can apply its own fallback policy. */
            cJSON_Delete(root);
            return false;
        }
        payload_bytes = payload_json;
        payload_len = strlen(payload_json);
    }

    client->ai_ctrl_callback(type->valuestring, payload_bytes, payload_len,
                             client->ai_ctrl_user_data);

    if (payload_json) client->pal->free(payload_json);
    cJSON_Delete(root);
    return true;
}

int iot_ai_ctrl_set_callback(iot_client_t *client,
                             ai_ctrl_callback_t callback, void *user_data)
{
    if (!client) return OPRT_INVALID_PARAMETER;

    client->ai_ctrl_callback = callback;
    client->ai_ctrl_user_data = user_data;
    return OPRT_OK;
}
