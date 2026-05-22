#include "iot_on_boarding.h"
#include "iot_client.h"
#include "mqtt.h"
#include "iot_dns.h"
#include "iot_config_defaults.h"
#include "cJSON.h"
#include "cipher_wrapper.h"
#include "atop.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mbedtls/base64.h>

typedef struct {
    char *https_url;
    iot_region_t region;
    iot_env_t env;
    char token[64];
    char secret[5];
} activation_message_t;

// Internal activation context
typedef struct {
    bool received_activation_message;
    activation_message_t *message;
    char authkey[64];  // For decryption in callback
    const pal_t *pal;
} activate_context_t;

// Global context for activation callback
static volatile activate_context_t *g_activate_ctx = NULL;
static volatile bool g_on_boarding_in_progress = false;

static void activate_context_destroy(void);

// Parse activation response JSON and extract URLs
static void parse_activation_message(const pal_t *pal, const char *json_str, activation_message_t *message) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        log_error("Failed to parse activation response JSON");
        return;
    }

    // Get data object
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        log_error("Activation response missing 'data' field");
        cJSON_Delete(root);
        return;
    }

    // Extract httpsUrl
    cJSON *https_url = cJSON_GetObjectItem(data, "httpsUrl");
    if (https_url && cJSON_IsString(https_url)) {
        message->https_url = pal_strdup(pal, https_url->valuestring);
        if (message->https_url) {
            log_info("Activation httpsUrl: %s", message->https_url);
        }
    }

    // Extract region
    cJSON *region = cJSON_GetObjectItem(data, "region");
    if (region && cJSON_IsString(region)) {
        const char *r = region->valuestring;
        if (strcmp(r, "AY") == 0) {
            message->region = AY;
        } else if (strcmp(r, "US") == 0) {
            message->region = US;
        } else if (strcmp(r, "EU") == 0) {
            message->region = EU;
        } else if (strcmp(r, "IN") == 0) {
            message->region = IN;
        } else if (strcmp(r, "SG") == 0) {
            message->region = SG;
        } else if (strcmp(r, "UEAZ") == 0) {
            message->region = UEAZ;
        } else if (strcmp(r, "WEAZ") == 0) {
            message->region = WEAZ;
        } else {
            log_error("Invalid region: %s", r);
            cJSON_Delete(root);
            return;
        }
        log_info("on boarding region: %s", r);
    } else {
        log_error("Activation response missing 'region' field");
        cJSON_Delete(root);
        return;
    }

    // Extract token
    cJSON *token = cJSON_GetObjectItem(data, "token");
    if (token && cJSON_IsString(token)) {
        strncpy(message->token, token->valuestring, sizeof(message->token) - 1);
        message->token[sizeof(message->token) - 1] = '\0';
        log_info("Activation token received");
    }

    cJSON_Delete(root);
}

// Internal message callback that stores response
static void internal_message_callback(const char *topic, size_t topic_len,
                                      const uint8_t *payload, size_t payload_len,
                                      void *user_data) {
    (void)user_data;
    log_info(">>> internal_message_callback called! topic_len=%u, payload_len=%u", (unsigned)topic_len, (unsigned)payload_len);

    if (!g_activate_ctx) {
        log_warn("g_activate_ctx is NULL!");
        return;
    }

    log_info("Received activation message on topic: %.*s", (int)topic_len, topic);
    log_debug("Base64 encoded payload (%u bytes)", (unsigned)payload_len);

    const pal_t *pal = g_activate_ctx->pal;

    uint8_t *base64_decoded = NULL;
    uint8_t *decrypted = NULL;
    size_t decrypted_len = 0;
    const uint8_t *final_payload = payload;
    size_t final_len = payload_len;

    // Step 1: Base64 decode the payload
    if (payload_len > 0) {
        size_t decoded_len = 0;
        int ret = mbedtls_base64_decode(NULL, 0, &decoded_len, payload, payload_len);
        if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && decoded_len > 0) {
            base64_decoded = (uint8_t *)pal->malloc(decoded_len);
            if (base64_decoded) {
                ret = mbedtls_base64_decode(base64_decoded, decoded_len, &decoded_len, payload, payload_len);
                if (ret == 0) {
                    log_info("Base64 decoded: %u bytes", (unsigned)decoded_len);
                    final_payload = base64_decoded;
                    final_len = decoded_len;
                } else {
                    log_warn("Base64 decode failed (ret=%d), using raw payload", ret);
                    pal->free(base64_decoded);
                    base64_decoded = NULL;
                }
            }
        } else {
            log_debug("Base64 decode size check failed (ret=%d), assuming raw data", ret);
        }
    }

    // Step 2: Decrypt the payload using P2.3 protocol with authkey (first 16 bytes)
    const char *authkey = (const char *)g_activate_ctx->authkey;
    if (authkey[0] != '\0' && strlen(authkey) >= 16 && final_len > 0) {
        decrypted = (uint8_t *)pal->malloc(final_len);
        if (decrypted) {
            // Use first 16 bytes of authkey as decryption key
            int ret = pv23_decrypt(final_payload, final_len,
                                  (const uint8_t *)authkey,
                                  decrypted, &decrypted_len);
            if (ret == 0) {
                log_info("Decrypted payload (%u bytes): %.*s", (unsigned)decrypted_len, (int)decrypted_len, decrypted);
                final_payload = decrypted;
                final_len = decrypted_len;
            } else {
                log_warn("Failed to decrypt payload (ret=%d), using base64 decoded data", ret);
                pal->free(decrypted);
                decrypted = NULL;
            }
        }
    } else {
        log_debug("No authkey provided or authkey too short, storing decoded payload");
    }

    activation_message_t message = {0};
    // Step 3: Parse JSON and extract activation URLs
    if (final_len > 0) {
        char *json_str = (char *)pal->malloc(final_len + 1);
        if (json_str) {
            memcpy(json_str, final_payload, final_len);
            json_str[final_len] = '\0';
            parse_activation_message(pal, json_str, &message);
            pal->free(json_str);
        }
    }

    // Store result in global context
    if (g_activate_ctx && message.token[0] != '\0') {
        g_activate_ctx->message = (activation_message_t *)pal->malloc(sizeof(activation_message_t));
        if (g_activate_ctx->message) {
            memcpy(g_activate_ctx->message, &message, sizeof(activation_message_t));
            g_activate_ctx->received_activation_message = true;
            if (message.https_url) {
                g_activate_ctx->message->https_url = pal_strdup(pal, message.https_url);
            } else {
                g_activate_ctx->message->https_url = NULL;
            }
            log_info("Activation message stored in context");
        }
    }

    if (message.https_url) {
        pal->free(message.https_url);
        message.https_url = NULL;
    }

    if (decrypted) {
        pal->free(decrypted);
    }
    if (base64_decoded) {
        pal->free(base64_decoded);
    }
}

static int activate_device(const pal_t *pal, on_boarding_config_t *on_boarding, const activation_message_t *act_msg, on_boarding_response_t *response) {
    if (!on_boarding || !response) {
        log_error("Invalid parameters for on boarding");
        return OPRT_INVALID_PARAMETER;
    }

    activite_request_t request = {0};
    request.token = act_msg->token;
    request.sw_ver = on_boarding->sw_ver;
    request.product_key = on_boarding->product_key;
    request.pv = on_boarding->pv;
    request.bv = on_boarding->bv;
    request.authkey = on_boarding->authkey;
    request.uuid = on_boarding->uuid;

    if (on_boarding->modules && on_boarding->modules[0] != '\0') {
        request.modules = on_boarding->modules;
    }
    if (on_boarding->feature && on_boarding->feature[0] != '\0') {
        request.feature = on_boarding->feature;
    }
    if (on_boarding->skill_param && on_boarding->skill_param[0] != '\0') {
        request.skill_param = on_boarding->skill_param;
    }
    request.sdk_version = SDK_VERSION;
    if (on_boarding->firmware_key[0] != '\0') {
        request.firmware_key = on_boarding->firmware_key;
    }
    request.user_data = NULL;

    char *parsed_host = NULL;
    request.host = IOT_DEFAULT_HOST;
    request.port = IOT_DEFAULT_PORT;

    if (act_msg->https_url && act_msg->https_url[0] != '\0') {
        const char *host_start = strstr(act_msg->https_url, "://");
        if (host_start) {
            host_start += 3;
            const char *path_start = strchr(host_start, '/');
            const char *port_start = strchr(host_start, ':');
            if (port_start && (!path_start || port_start < path_start)) {
                size_t hlen = port_start - host_start;
                parsed_host = (char *)pal->malloc(hlen + 1);
                if (parsed_host) {
                    memcpy(parsed_host, host_start, hlen);
                    parsed_host[hlen] = '\0';
                    request.host = parsed_host;
                    request.port = (uint16_t)atoi(port_start + 1);
                }
            } else {
                const char *end = path_start ? path_start : host_start + strlen(host_start);
                size_t hlen = end - host_start;
                parsed_host = (char *)pal->malloc(hlen + 1);
                if (parsed_host) {
                    memcpy(parsed_host, host_start, hlen);
                    parsed_host[hlen] = '\0';
                    request.host = parsed_host;
                    request.port = IOT_DEFAULT_PORT;
                }
            }
        } else {
            request.host = act_msg->https_url;
            request.port = IOT_DEFAULT_PORT;
        }
    }
    request.cacert = on_boarding->cacert;

    log_info("Sending activation request with:");
    log_info("  - Token: [%zu chars, prefix=%.4s...]",
             request.token ? strlen(request.token) : 0,
             (request.token && strlen(request.token) >= 4) ? request.token : "----");
    log_info("  - Software Version: %s", request.sw_ver);
    log_info("  - Product Key: %s", request.product_key);
    log_info("  - Protocol Version: %s", request.pv);
    log_info("  - Baseline Version: %s", request.bv);
    log_info("  - UUID: %s", request.uuid);
    if (request.host) {
        log_info("  - Server: %s:%d", request.host, request.port > 0 ? request.port : 443);
    } else {
        log_info("  - Server: (default)");
    }
    if (request.devid) {
        log_info("  - Device ID: %s", request.devid);
    }
    if (request.modules) {
        log_info("  - Modules: %s", request.modules);
    }
    if (request.feature) {
        log_info("  - Feature: %s", request.feature);
    }
    if (request.skill_param) {
        log_info("  - Skill Param: %s", request.skill_param);
    }
    if (request.firmware_key) {
        log_info("  - Firmware Key: (set)");
    }

    // Send activation request
    activite_response_t activate_response = {0};
    int ret = atop_activate_request(pal, &request, &activate_response);

    if (ret == OPRT_OK) {
        log_info("✓ Activation successful!");

        log_info("Device ID: %s", activate_response.devid);

    } else {
        log_error("✗ Activation request failed with error code: %d", ret);

        // Map error codes to readable messages
        switch (ret) {
            case OPRT_INVALID_PARAMETER:
                log_error("  - Invalid parameters");
                break;
            case OPRT_COMMUNICATION_ERROR:
                log_error("  - Communication error");
                break;
            case OPRT_TLS_HANDSHAKE_FAILED:
                log_error("  - TLS handshake failed");
                break;
            default:
                log_error("  - Unknown error");
                break;
        }
    }

    if (ret == OPRT_OK) {
        if (activate_response.devid) {
            strncpy(response->devid, activate_response.devid, sizeof(response->devid) - 1);
            response->devid[sizeof(response->devid) - 1] = '\0';
        }
        if (activate_response.secret_key) {
            strncpy(response->secret_key, activate_response.secret_key, sizeof(response->secret_key) - 1);
            response->secret_key[sizeof(response->secret_key) - 1] = '\0';
        }
        if (activate_response.local_key) {
            strncpy(response->local_key, activate_response.local_key, sizeof(response->local_key) - 1);
            response->local_key[sizeof(response->local_key) - 1] = '\0';
        }
        if (activate_response.schema_id) {
            strncpy(response->schema_id, activate_response.schema_id, sizeof(response->schema_id) - 1);
            response->schema_id[sizeof(response->schema_id) - 1] = '\0';
        }
        if (activate_response.schema)
            response->schema = pal_strdup(pal, activate_response.schema);
        response->region = act_msg->region;
        response->env = act_msg->env;
    }

    atop_activate_response_free(pal, &activate_response);
    pal->free(parsed_host);
    return ret;
}

/**
 * @brief Parse region from the first 2 characters of a token
 * @param[in] token activation token whose prefix encodes the region
 * @param[out] region output region value
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if prefix is unknown
 */
static int __token_to_region(const char *token, iot_region_t *region)
{
    if (strlen(token) < 2) {
        return OPRT_INVALID_PARAMETER;
    }

    char prefix[3] = { token[0], token[1], '\0' };

    if (strcmp(prefix, "AY") == 0) {
        *region = AY;
    } else if (strcmp(prefix, "US") == 0) {
        *region = US;
    } else if (strcmp(prefix, "UE") == 0) {
        *region = UEAZ;
    } else if (strcmp(prefix, "EU") == 0) {
        *region = EU;
    } else if (strcmp(prefix, "WE") == 0) {
        *region = WEAZ;
    } else if (strcmp(prefix, "IN") == 0) {
        *region = IN;
    } else if (strcmp(prefix, "SG") == 0) {
        *region = SG;
    } else {
        log_error("Unknown region prefix in token: %s", prefix);
        return OPRT_INVALID_PARAMETER;
    }
    return OPRT_OK;
}

int on_boarding_with_token(const pal_t *pal, on_boarding_config_t *on_boarding,
                           const char *token, on_boarding_response_t *response)
{
    if (!on_boarding || !token || token[0] == '\0' || !response) {
        log_error("Invalid parameters for on_boarding_with_token");
        return OPRT_INVALID_PARAMETER;
    }

    size_t token_len = strlen(token);
    if (token_len < 7) {
        log_error("Token too short: need at least 7 chars (2 region + token + 4 secret)");
        return OPRT_INVALID_PARAMETER;
    }

    iot_region_t region;
    int ret = __token_to_region(token, &region);
    if (ret != OPRT_OK) {
        log_error("Failed to parse region from token prefix");
        return ret;
    }

    activation_message_t act_msg = {0};
    size_t act_token_len = token_len - 2 - 4;
    memcpy(act_msg.secret, token + token_len - 4, 4);
    act_msg.secret[4] = '\0';
    if (act_token_len >= sizeof(act_msg.token)) {
        act_token_len = sizeof(act_msg.token) - 1;
    }
    memcpy(act_msg.token, token + 2, act_token_len);
    act_msg.token[act_token_len] = '\0';
    act_msg.region = region;
    act_msg.env = on_boarding->env;
    act_msg.https_url = iot_region_to_host(region, on_boarding->env);

    log_info("on_boarding_with_token: region=%d env=%d",
             region, on_boarding->env);

    return activate_device(pal, on_boarding, &act_msg, response);
}

static int activate_context_init(const pal_t *pal) {
    if (g_activate_ctx) {
        activate_context_destroy();
    }
    g_activate_ctx = (activate_context_t *)pal->malloc(sizeof(activate_context_t));
    if (!g_activate_ctx) {
        log_error("Failed to allocate activate context");
        return OPRT_MALLOC_FAILED;
    }
    g_activate_ctx->received_activation_message = false;
    g_activate_ctx->message = NULL;
    g_activate_ctx->pal = pal;
    memset((void *)g_activate_ctx->authkey, 0, sizeof(g_activate_ctx->authkey));
    return OPRT_OK;
}

static void activate_context_destroy() {
    if (!g_activate_ctx) {
        log_error("Activate context is not initialized");
        return;
    }
    const pal_t *pal = g_activate_ctx->pal;
    if (g_activate_ctx->message) {
        pal->free(g_activate_ctx->message->https_url);
        pal->free(g_activate_ctx->message);
    }
    pal->free((void *)g_activate_ctx);
    g_activate_ctx = NULL;
}

int on_boarding_with_qrcode(const pal_t *pal, on_boarding_config_t *on_boarding, on_boarding_response_t *response) {
    if (!on_boarding || !response) {
        log_error("Invalid parameters for on boarding");
        return OPRT_INVALID_PARAMETER;
    }

    if (g_on_boarding_in_progress) {
        log_error("on_boarding_with_qrcode: already in progress");
        return OPRT_NOT_SUPPORTED;
    }
    g_on_boarding_in_progress = true;

    if (activate_context_init(pal) != 0) {
        log_error("Failed to initialize activate context");
        return OPRT_MALLOC_FAILED;
    }

    // Copy authkey to context for use in callback
    strncpy((char *)g_activate_ctx->authkey, on_boarding->authkey, sizeof(g_activate_ctx->authkey) - 1);

    int ret = 0;
    char *client_id = NULL;
    char *username = NULL;
    char *subscribe_topic = NULL;
    char *broker_url = NULL;
    mqtt_client *client = NULL;

    // Build MQTT connection parameters
    size_t uuid_len = strlen(on_boarding->uuid);

    // clientId: acon_{uuid}
    client_id = (char *)pal->malloc(6 + uuid_len);
    if (!client_id) { ret = OPRT_MALLOC_FAILED; goto end; }
    int sn = snprintf(client_id, 6 + uuid_len, "acon_%s", on_boarding->uuid);
    if (sn < 0 || (size_t)sn >= 6 + uuid_len) { ret = OPRT_COMMUNICATION_ERROR; goto end; }

    // username: acon_{uuid}|pv=2.3
    username = (char *)pal->malloc(14 + uuid_len);
    if (!username) { ret = OPRT_MALLOC_FAILED; goto end; }
    sn = snprintf(username, 14 + uuid_len, "acon_%s|pv=2.3", on_boarding->uuid);
    if (sn < 0 || (size_t)sn >= 14 + uuid_len) { ret = OPRT_COMMUNICATION_ERROR; goto end; }

    // password: first 16 chars of MD5(authkey)
    char password[17];
    if (iot_md5_password(on_boarding->authkey, password) != 0) {
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }

    // subscribe topic: d/ai/{uuid}
    subscribe_topic = (char *)pal->malloc(6 + uuid_len);
    if (!subscribe_topic) { ret = OPRT_MALLOC_FAILED; goto end; }
    sn = snprintf(subscribe_topic, 6 + uuid_len, "d/ai/%s", on_boarding->uuid);
    if (sn < 0 || (size_t)sn >= 6 + uuid_len) { ret = OPRT_COMMUNICATION_ERROR; goto end; }

    log_info("Device activation starting...");
    log_info("  subscribe_topic: %s", subscribe_topic);


    // Query MQTT endpoint from IoT DNS service
    const char *mqtt_dns_key = on_boarding->mqtt_disable_tls ? "mqttUrl" : "mqttsUrl";
    iot_dns_config_item_t dns_keys[] = {
        { .key = mqtt_dns_key, .need_ca = !on_boarding->mqtt_disable_tls },
    };
    iot_dns_url_config_request_t dns_req = {
        .host = on_boarding->dns_host,
        .port = on_boarding->dns_port,
        .cacert = on_boarding->cacert,
        .env = on_boarding->env == PRE ? "pre" : "prod",
        .uuid = on_boarding->uuid,
        .config = dns_keys,
        .config_count = 1,
    };
    iot_dns_url_config_response_t dns_resp = {0};
    int dns_ret = iot_dns_url_config(pal, &dns_req, &dns_resp);
    if (dns_ret != OPRT_OK) {
        log_error("Failed to query IoT DNS for MQTT endpoint: %d", dns_ret);
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }

    const char *mqtt_addr = NULL;
    for (int i = 0; i < dns_resp.endpoint_count; i++) {
        if (strcmp(dns_resp.endpoints[i].key, mqtt_dns_key) == 0) {
            mqtt_addr = dns_resp.endpoints[i].addr;
            break;
        }
    }
    if (!mqtt_addr) {
        log_error("IoT DNS returned no %s endpoint", mqtt_dns_key);
        iot_dns_url_config_response_free(pal, &dns_resp);
        ret = OPRT_INVALID_RESULT;
        goto end;
    }

    const char *scheme = on_boarding->mqtt_disable_tls ? "mqtt" : "mqtts";
    size_t broker_url_len = strlen(scheme) + 3 + strlen(mqtt_addr) + 1;
    broker_url = (char *)pal->malloc(broker_url_len);
    if (!broker_url) {
        iot_dns_url_config_response_free(pal, &dns_resp);
        ret = OPRT_MALLOC_FAILED;
        goto end;
    }
    sn = snprintf(broker_url, broker_url_len, "%s://%s", scheme, mqtt_addr);
    if (sn < 0 || (size_t)sn >= broker_url_len) {
        iot_dns_url_config_response_free(pal, &dns_resp);
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }
    log_info("MQTT broker URL from DNS: %s", broker_url);

    mqtt_tls_config_t tls_config = {0};
    if (!on_boarding->mqtt_disable_tls) {
        if (on_boarding->cacert && on_boarding->cacert[0] != '\0') {
            tls_config.cacert = on_boarding->cacert;
        } else {
            log_warn("MQTT TLS without CA certificate - server verification disabled");
        }
        tls_config.verify_peer = false;
    }

    mqtt_client_config_t mqtt_config = {
        .broker_url = broker_url,
        .client_id = client_id,
        .username = username,
        .password = password,
        .subscribe_topic = subscribe_topic,
        .callback = internal_message_callback,
        .user_data = (void *)g_activate_ctx,
        .tls_config = &tls_config,
        .pal = pal,
    };

    // Create MQTT client before freeing DNS response
    client = mqtt_client_create_with_config(&mqtt_config);
    iot_dns_url_config_response_free(pal, &dns_resp);
    if (!client) {
        log_error("Failed to create MQTT client for activation");
        ret = OPRT_MALLOC_FAILED;
        goto end;
    }

    // Connect to broker
    if (mqtt_client_connect(client) != 0) {
        log_error("Failed to connect to MQTT broker");
        mqtt_client_destroy(client);
        client = NULL;
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }

    // Subscribe to activation topic
    if (mqtt_client_subscribe(client) != 0) {
        log_error("Failed to subscribe to activation topic");
        mqtt_client_disconnect(client);
        mqtt_client_destroy(client);
        client = NULL;
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }

    log_info("Subscribed to %s, waiting for activation message...", subscribe_topic);

    // Wait for activation message
    uint32_t elapsed_ms = 0;
    uint32_t poll_interval_ms = 100;

    uint32_t timeout_ms = on_boarding->timeout_ms > 0 ? on_boarding->timeout_ms : 0xFFFFFFFF;
    while (!g_activate_ctx->received_activation_message && elapsed_ms < timeout_ms) {
        if (mqtt_client_process(client, poll_interval_ms) != 0) {
            log_warn("MQTT process returned error");
        }
        elapsed_ms += poll_interval_ms;
    }
    mqtt_client_disconnect(client);
    mqtt_client_destroy(client);
    client = NULL;

    if (elapsed_ms >= timeout_ms) {
        log_error("Timeout waiting for activation message");
        ret = OPRT_COMMUNICATION_ERROR;
        goto end;
    }

    g_activate_ctx->message->env = on_boarding->env;
    ret = activate_device(pal, on_boarding, g_activate_ctx->message, response);
    if (ret != OPRT_OK) {
        log_error("Failed to activate device: %d", ret);
    }
end:
    pal->free(client_id);
    pal->free(username);
    pal->free(subscribe_topic);
    pal->free(broker_url);
    activate_context_destroy();
    g_on_boarding_in_progress = false;
    return ret;
}
