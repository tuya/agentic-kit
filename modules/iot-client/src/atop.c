#include "atop.h"
#include "atop_base.h"
#include "cJSON.h"
#include "iot_config_defaults.h"
#include "cipher_wrapper.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>



/* ============================================================================
 * ATOP Service Implementation (based on atop_base)
 * ============================================================================ */

#define CAD_VER       "1.0.3"
#define CD_VER        "1.0.0"
#define ATTRIBUTE_OTA (11)


/**
 * @brief Sends an activate request to the ATOP service.
 *
 * This function sends an activate request to the ATOP service using the
 * provided request parameters.
 *
 * @param request The activate request parameters.
 * @param response The response structure to store the result.
 * @return Returns OPRT_OK if the request is successful, otherwise returns an
 * error code.
 */
int atop_activate_request(const pal_t *pal, const activite_request_t *request, activite_response_t *response)
{
    if (request == NULL || response == NULL) {
        log_error("atop_activate_request: request or response is NULL");
        return OPRT_INVALID_PARAMETER;
    }

    #define CHECK_STR_PARAM(field) \
        if (request->field == NULL || request->field[0] == '\0') { \
            log_error("atop_activate_request: invalid parameter '%s'", #field); \
            return OPRT_INVALID_PARAMETER; \
        }
    CHECK_STR_PARAM(token);
    CHECK_STR_PARAM(sw_ver);
    CHECK_STR_PARAM(product_key);
    CHECK_STR_PARAM(pv);
    CHECK_STR_PARAM(bv);
    CHECK_STR_PARAM(authkey);
    CHECK_STR_PARAM(uuid);
    #undef CHECK_STR_PARAM

    int rt = OPRT_OK;

    /* post data */
    #define ACTIVATE_POST_BUFFER_LEN (255)
    size_t prealloc_size = ACTIVATE_POST_BUFFER_LEN;

    if (request->devid) {
        prealloc_size += strlen(request->devid) + 10;
    }

    if (request->modules) {
        prealloc_size += strlen(request->modules) + 10;
    }

    if (request->feature) {
        prealloc_size += strlen(request->feature) + 10;
    }

    if (request->skill_param) {
        prealloc_size += strlen(request->skill_param) + 10;
    }

    if (request->firmware_key) {
        prealloc_size += strlen(request->firmware_key) + 10;
    }
    if (request->sdk_version) {
        prealloc_size += strlen(request->sdk_version) + 24;
    }

    char *buffer = (char *)pal->malloc(prealloc_size);
    if (NULL == buffer) {
        log_error("post buffer malloc fail");
        return OPRT_MALLOC_FAILED;
    }
    uint32_t timestamp = (uint32_t)time(NULL);

    /* activate JSON format */
    size_t offset = 0;
    size_t remain = prealloc_size;

    /* Requires params */
    int write_len = snprintf(buffer + offset, remain,
                             "{\"token\":\"%s\",\"softVer\":\"%s\",\"productKey\":\"%s\",\"protocolVer\":\"%s\","
                             "\"baselineVer\":\"%s\"",
                             request->token, request->sw_ver, request->product_key, request->pv, request->bv);
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;
    remain = prealloc_size - offset;

    /* option params */
    if (request->sdk_version && request->sdk_version[0]) {
        write_len = snprintf(buffer + offset, remain,
                             ",\"options\":\"{\\\"otaChannel\\\":0,\\\"sdkFullVer\\\":\\\"%s\\\",",
                             request->sdk_version);
    } else {
        write_len = snprintf(buffer + offset, remain, ",\"options\":\"{\\\"otaChannel\\\":0,");
    }
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;
    remain = prealloc_size - offset;

    if (request->firmware_key && request->firmware_key[0]) {
        write_len = snprintf(buffer + offset, remain, "\\\"isFK\\\":true");
    } else {
        write_len = snprintf(buffer + offset, remain, "\\\"isFK\\\":false");
    }
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;
    remain = prealloc_size - offset;

    write_len = snprintf(buffer + offset, remain, "}\"");
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;
    remain = prealloc_size - offset;

    /* firmware_key */
    if (request->firmware_key && request->firmware_key[0]) {
        write_len = snprintf(buffer + offset, remain, ",\"productKeyStr\":\"%s\"", request->firmware_key);
        if (write_len < 0 || (size_t)write_len >= remain) {
            pal->free(buffer);
            return OPRT_COMMUNICATION_ERROR;
        }
        offset += (size_t)write_len;
        remain = prealloc_size - offset;
    }

    /* Activated atop */
    if (request->devid && strlen(request->devid) > 0) {
        write_len = snprintf(buffer + offset, remain, ",\"devId\":\"%s\"", request->devid);
        if (write_len < 0 || (size_t)write_len >= remain) {
            pal->free(buffer);
            return OPRT_COMMUNICATION_ERROR;
        }
        offset += (size_t)write_len;
        remain = prealloc_size - offset;
    }

    /* modules */
    if (request->modules && strlen(request->modules) > 0) {
        write_len = snprintf(buffer + offset, remain, ",\"modules\":\"%s\"", request->modules);
        if (write_len < 0 || (size_t)write_len >= remain) {
            pal->free(buffer);
            return OPRT_COMMUNICATION_ERROR;
        }
        offset += (size_t)write_len;
        remain = prealloc_size - offset;
    }

    /* feature */
    if (request->feature && strlen(request->feature) > 0) {
        write_len = snprintf(buffer + offset, remain, ",\"feature\":\"%s\"", request->feature);
        if (write_len < 0 || (size_t)write_len >= remain) {
            pal->free(buffer);
            return OPRT_COMMUNICATION_ERROR;
        }
        offset += (size_t)write_len;
        remain = prealloc_size - offset;
    }

    /* skill_param */
    if (request->skill_param && strlen(request->skill_param) > 0) {
        write_len = snprintf(buffer + offset, remain, ",\"skillParam\":\"%s\"", request->skill_param);
        if (write_len < 0 || (size_t)write_len >= remain) {
            pal->free(buffer);
            return OPRT_COMMUNICATION_ERROR;
        }
        offset += (size_t)write_len;
        remain = prealloc_size - offset;
    }

    /* default support device OTA */
    write_len = snprintf(buffer + offset, remain, ",\"devAttribute\":%u", 1 << ATTRIBUTE_OTA);
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;
    remain = prealloc_size - offset;

    write_len =
        snprintf(buffer + offset, remain, ",\"cadVer\":\"%s\",\"cdVer\":\"%s\",\"t\":%" PRIu32 "}", CAD_VER, CD_VER,
                 timestamp);
    if (write_len < 0 || (size_t)write_len >= remain) {
        pal->free(buffer);
        return OPRT_COMMUNICATION_ERROR;
    }
    offset += (size_t)write_len;

    log_info("POST JSON:%s", buffer);

    /* atop_base_request object construct */
    atop_base_request_t atop_request = {.uuid = request->uuid,
                                        .key = request->authkey,
                                        .path = "/d.json",
                                        .timestamp = timestamp,
                                        .api = "thing.device.opensdk.active",
                                        .version = "1.0",
                                        .data = (void *)buffer,
                                        .datalen = offset,
                                        .user_data = request->user_data,
                                        .host = request->host,
                                        .port = request->port,
                                        .cacert = request->cacert};

    /* ATOP service request send */
    atop_base_response_t *atop_response = pal->malloc(sizeof(atop_base_response_t));
    if (atop_response == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    rt = atop_base_request(pal,&atop_request, atop_response);
    pal->free(buffer);
    if (OPRT_OK != rt) {
        log_error("atop_base_request error:%d", rt);
        pal->free(atop_response);
        return rt;
    }

    cJSON *result = atop_response->result;
    if (result == NULL) {
        log_error("activate response no result");
        atop_base_response_free(pal,atop_response);
        pal->free(atop_response);
        return OPRT_COMMUNICATION_ERROR;
    }

    memset(response, 0, sizeof(activite_response_t));

    response->devid = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "devId")));
    response->secret_key = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "secKey")));
    response->local_key = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "localKey")));
    response->product_key = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "productKey")));
    response->pv = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "pv")));
    response->bv = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "bv")));
    response->uuid = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "uuid")));
    response->schema_id = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "schemaId")));
    response->schema = pal_strdup(pal, cJSON_GetStringValue(cJSON_GetObjectItem(result, "schema")));

    if (!response->devid || !response->secret_key || !response->local_key) {
        log_error("activate response missing required fields");
        atop_activate_response_free(pal, response);
        atop_base_response_free(pal,atop_response);
        pal->free(atop_response);
        return OPRT_COMMUNICATION_ERROR;
    }

    atop_base_response_free(pal,atop_response);
    pal->free(atop_response);
    return rt;
}

void atop_activate_response_free(const pal_t *pal, activite_response_t *response)
{
    if (response == NULL) {
        return;
    }
    pal->free((void *)response->devid);
    pal->free((void *)response->secret_key);
    pal->free((void *)response->local_key);
    pal->free((void *)response->product_key);
    pal->free((void *)response->pv);
    pal->free((void *)response->bv);
    pal->free((void *)response->uuid);
    pal->free((void *)response->schema_id);
    pal->free((void *)response->schema);
    memset(response, 0, sizeof(activite_response_t));
}

/* ============================================================================
 * Device Meta Save Service Implementation
 * ============================================================================ */

#define ATOP_DEVICE_META_SAVE "tuya.device.meta.save"

int atop_device_meta_save(const pal_t *pal, const device_meta_save_request_t *request, device_meta_save_response_t *response)
{
    if (request == NULL || response == NULL) {
        log_error("atop_device_meta_save: request or response is NULL");
        return OPRT_INVALID_PARAMETER;
    }

    if (request->devid == NULL || request->devid[0] == '\0' ||
        request->key == NULL || request->key[0] == '\0' ||
        request->sdk_version == NULL || request->sdk_version[0] == '\0') {
        log_error("atop_device_meta_save: devid, key, or sdk_version is empty");
        return OPRT_INVALID_PARAMETER;
    }

    memset(response, 0, sizeof(device_meta_save_response_t));

    uint32_t timestamp = (uint32_t)time(NULL);

    /* {"t":12345,"metas":"{\"smain_network_sdk_full_version\":\"agentic-kit 0.1\"}"} */
    size_t post_len = 64 + strlen(request->sdk_version) * 2;
    char *post_data = (char *)pal->malloc(post_len);
    if (post_data == NULL) {
        log_error("atop_device_meta_save: malloc failed");
        return OPRT_MALLOC_FAILED;
    }
    int write_len = snprintf(post_data, post_len,
        "{\"t\":%" PRIu32 ",\"metas\":\"{\\\"main_network_sdk_full_version\\\":\\\"%s\\\"}\"}",
        timestamp, request->sdk_version);
    if (write_len < 0 || (size_t)write_len >= post_len) {
        pal->free(post_data);
        return OPRT_COMMUNICATION_ERROR;
    }

    log_debug("device meta save post data:%s", post_data);

    atop_base_request_t atop_request = {
        .devid = request->devid,
        .key = request->key,
        .path = "/d.json",
        .timestamp = timestamp,
        .api = ATOP_DEVICE_META_SAVE,
        .version = "1.0",
        .data = (void *)post_data,
        .datalen = strlen(post_data),
        .user_data = NULL,
        .host = request->host,
        .port = request->port,
        .cacert = request->cacert,
    };

    atop_base_response_t atop_response = {0};
    int rt = atop_base_request(pal, &atop_request, &atop_response);
    pal->free(post_data);

    if (rt != OPRT_OK) {
        log_error("atop_device_meta_save request error:%d", rt);
        atop_base_response_free(pal, &atop_response);
        return rt;
    }

    cJSON *result = atop_response.result;
    if (result == NULL) {
        log_error("atop_device_meta_save: no result");
        atop_base_response_free(pal, &atop_response);
        return OPRT_COMMUNICATION_ERROR;
    }

    response->success = true;
    atop_base_response_free(pal, &atop_response);
    return OPRT_OK;
}

/* ============================================================================
 * QR Code Info Service Implementation
 * ============================================================================ */

#define ATOP_QRCODE_INFO_GET "tuya.device.qrcode.info.get"

/**
 * @brief Get QR code info from Tuya cloud
 *
 * @param[in]  request  Request parameters (uuid, authkey, app_id, type)
 * @param[out] response Response containing QR code URL (caller must free url)
 * @return OPRT_OK on success, error code on failure
 */
int atop_qrcode_info_get(const pal_t *pal, const qrcode_info_request_t *request, qrcode_info_response_t *response)
{
    if (request == NULL || response == NULL) {
        log_error("atop_qrcode_info_get: request or response is NULL");
        return OPRT_INVALID_PARAMETER;
    }

    if (request->authkey == NULL || request->authkey[0] == '\0') {
        log_error("atop_qrcode_info_get: authkey is empty");
        return OPRT_INVALID_PARAMETER;
    }

    memset(response, 0, sizeof(qrcode_info_response_t));

    uint32_t timestamp = (uint32_t)time(NULL);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_error("atop_qrcode_info_get: failed to create JSON object");
        return OPRT_MALLOC_FAILED;
    }

    const char *app_id = (request->app_id != NULL) ? request->app_id : "";
    cJSON_AddStringToObject(root, "appId", app_id);
    cJSON_AddNumberToObject(root, "t", timestamp);
    cJSON_AddNumberToObject(root, "type", request->type);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (post_data == NULL) {
        log_error("atop_qrcode_info_get: failed to print JSON");
        return OPRT_MALLOC_FAILED;
    }

    log_debug("qrcode info post data:%s", post_data);

    atop_base_request_t atop_request = {
        .uuid = request->uuid,
        .key = request->authkey,
        .path = "/d.json",
        .timestamp = timestamp,
        .api = ATOP_QRCODE_INFO_GET,
        .version = "1.1",
        .data = (void *)post_data,
        .datalen = strlen(post_data),
        .user_data = NULL,
        .host = request->host,
        .port = request->port,
        .cacert = request->cacert,
    };

    atop_base_response_t atop_response = {0};
    int rt = atop_base_request(pal, &atop_request, &atop_response);
    cJSON_free(post_data);

    if (rt != OPRT_OK) {
        log_error("atop_qrcode_info_get request error:%d", rt);
        atop_base_response_free(pal,&atop_response);
        return rt;
    }

    cJSON *result = atop_response.result;
    if (result == NULL) {
        log_error("atop_qrcode_info_get: no result");
        atop_base_response_free(pal,&atop_response);
        return OPRT_COMMUNICATION_ERROR;
    }

    cJSON *short_url = cJSON_GetObjectItem(result, "shortUrl");
    if (short_url == NULL || !cJSON_IsString(short_url)) {
        log_error("atop_qrcode_info_get: missing short_url in result");
        atop_base_response_free(pal,&atop_response);
        return OPRT_INVALID_RESULT;
    }
    response->short_url = pal_strdup(pal, short_url->valuestring);

    atop_base_response_free(pal,&atop_response);

    if (response->short_url == NULL) {
        log_error("atop_qrcode_info_get: failed to allocate memory for short_url");
        return OPRT_MALLOC_FAILED;
    }

    log_debug("qrcode info short_url: %s", response->short_url);
    return OPRT_OK;
}

/* ============================================================================
 * AI Configuration Service Implementation
 * ============================================================================ */

#define AI_ATOP_THING_AGENT_TOKEN_GET "thing.ai.agent.token.get"

/**
 * @brief Gets AI configuration information from Tuya cloud.
 *
 * This function sends a request to get AI configuration information including
 * server hosts, ports, credentials, and other connection parameters.
 *
 * @param request The request parameters (devid, key, agent_code).
 * @param config Output structure to store the configuration (caller must free using atop_ai_config_free).
 * @return Returns OPRT_OK if the request is successful, otherwise returns an error code.
 */
int atop_ai_token_get(const pal_t *pal, const ai_token_request_t *request, ai_token_response_t *response)
{
    if (request == NULL || response == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    if (request->devid == NULL || request->key == NULL ||
        request->devid[0] == '\0' || request->key[0] == '\0') {
        return OPRT_INVALID_PARAMETER;
    }

    memset(response, 0, sizeof(ai_token_response_t));

    int rt = OPRT_OK;
    uint32_t timestamp = (uint32_t)time(NULL);
    // Create request JSON
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_error("Failed to create JSON object");
        return OPRT_MALLOC_FAILED;
    }

    const char *agent_code = (request->agent_code != NULL) ? request->agent_code : "";
    cJSON_AddStringToObject(root, "agentCode", agent_code);
    cJSON_AddNumberToObject(root, "t", (double)timestamp);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (post_data == NULL) {
        log_error("Failed to print JSON");
        return OPRT_MALLOC_FAILED;
    }

    log_debug("post data:%s", post_data);

    // Prepare atop_base_request
    atop_base_request_t atop_request = {
        .devid = request->devid,
        .key = request->key,
        .path = "/d.json",
        .timestamp = timestamp,
        .api = AI_ATOP_THING_AGENT_TOKEN_GET,
        .version = "1.0",
        .data = (void *)post_data,
        .datalen = strlen(post_data),
        .user_data = NULL,
        .host = request->host,
        .port = request->port,
        .cacert = request->cacert,
    };

    atop_base_response_t atop_response = {0};
    rt = atop_base_request(pal, &atop_request, &atop_response);

    pal->free(post_data);

    if (rt != OPRT_OK) {
        log_error("http post err, rt:%d", rt);
        atop_base_response_free(pal,&atop_response);
        return rt;
    }

    cJSON *result = atop_response.result;
    if (result == NULL) {
        log_error("http post err, no result");
        atop_base_response_free(pal,&atop_response);
        return OPRT_COMMUNICATION_ERROR;
    }

    if (cJSON_IsString(result)) {
        response->token = pal_strdup(pal, result->valuestring);
    } else {
        char *json_str = cJSON_PrintUnformatted(result);
        if (json_str) {
            response->token = pal_strdup(pal, json_str);
            cJSON_free(json_str);
        }
    }
    atop_base_response_free(pal,&atop_response);

    if (response->token == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    log_debug("token: [%zu chars, prefix=%.4s...]",
              strlen(response->token),
              strlen(response->token) >= 4 ? response->token : "----");
    return OPRT_OK;
}
