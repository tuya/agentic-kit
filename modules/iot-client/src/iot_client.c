#include "iot_client.h"
#include "iot_dp.h"
#include "iot_dp_internal.h"
#include "iot_on_boarding.h"
#include "iot_dns.h"
#include "iot_client_message.h"
#include "iot_ota.h"
#include "cipher_wrapper.h"
#include "iot_config_defaults.h"
#include "rng.h"

#include "atop.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const pal_t *g_iot_pal = NULL;

static void set_pal(const pal_t *pal)
{
    g_iot_pal = pal;
}

static const pal_t* get_pal(void)
{
    return g_iot_pal;
}

static const char *iot_env_to_string(iot_env_t env)
{
    return env == PRE ? "pre" : "prod";
}

static const char *iot_region_to_string(iot_region_t region)
{
    switch (region) {
        case AY:   return "AY";
        case US:   return "US";
        case UEAZ: return "UEAZ";
        case EU:   return "EU";
        case WEAZ: return "WEAZ";
        case IN:   return "IN";
        case SG:   return "SG";
        default:   return NULL;
    }
}

static int parse_host_port(const char *url, char *host_out, size_t host_len, uint16_t *port_out)
{
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "mqtts://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;

    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');
    if (colon && (!slash || colon < slash)) {
        size_t hlen = colon - p;
        if (hlen >= host_len) hlen = host_len - 1;
        memcpy(host_out, p, hlen);
        host_out[hlen] = '\0';

        char *endptr = NULL;
        unsigned long port_val = strtoul(colon + 1, &endptr, 10);
        if (endptr == colon + 1 || port_val == 0 || port_val > 65535) {
            log_error("Invalid port in URL: %s", url);
            return OPRT_INVALID_PARAMETER;
        }
        if (*endptr != '\0' && *endptr != '/' && endptr != slash) {
            log_error("Invalid characters after port in URL: %s", url);
            return OPRT_INVALID_PARAMETER;
        }
        *port_out = (uint16_t)port_val;
    } else {
        const char *end = slash ? slash : p + strlen(p);
        size_t hlen = end - p;
        if (hlen >= host_len) hlen = host_len - 1;
        memcpy(host_out, p, hlen);
        host_out[hlen] = '\0';
        *port_out = 443;
    }
    return OPRT_OK;
}

/* Resolve the ATOP host/port for this client. Shared by iot_client_get_session_token()
 * and the DP-layer schema-update query (declared in iot_dp_internal.h). */
void iot_client_resolve_atop_host(iot_client_t *client, char *host_out, size_t host_len, uint16_t *port_out)
{
    *port_out = IOT_DEFAULT_PORT;
    if (host_len == 0) return;
    host_out[0] = '\0';
    if (client->https_url) {
        parse_host_port(client->https_url, host_out, host_len, port_out);
    } else {
        const char *h = iot_region_to_host(client->region, client->env);
        if (h) {
            strncpy(host_out, h, host_len - 1);
            host_out[host_len - 1] = '\0';
        }
    }
}

static int iot_client_dns_resolve(iot_client_t *client)
{
    const char *mqtt_dns_key = client->mqtt_disable_tls ? "mqttUrl" : "mqttsUrl";
    iot_dns_config_item_t dns_keys[] = {
        { .key = mqtt_dns_key },
        { .key = "httpsUrl" },
    };
    iot_dns_url_config_request_t dns_req = {
        .cacert = client->cacert,
        .host = NULL,
        .port = 0,
        .region = iot_region_to_string(client->region),
        .env = iot_env_to_string(client->env),
        .uuid = client->devid,
        .config = dns_keys,
        .config_count = 2,
    };
    iot_dns_url_config_response_t dns_resp = {0};
    int ret = iot_dns_url_config(client->pal, &dns_req, &dns_resp);
    if (ret != OPRT_OK) {
        log_warn("Failed to query IoT DNS for service URLs: %d", ret);
        return ret;
    }

    for (int i = 0; i < dns_resp.endpoint_count; i++) {
        if (strcmp(dns_resp.endpoints[i].key, mqtt_dns_key) == 0) {
            const char *scheme = client->mqtt_disable_tls ? "mqtt" : "mqtts";
            const char *addr = dns_resp.endpoints[i].addr;
            size_t url_len = strlen(scheme) + 3 + strlen(addr) + 1;
            char *mqtt_url = (char *)client->pal->malloc(url_len);
            if (mqtt_url) {
                int sn = snprintf(mqtt_url, url_len, "%s://%s", scheme, addr);
                if (sn < 0 || (size_t)sn >= url_len) {
                    client->pal->free(mqtt_url);
                } else {
                    client->mqtt_url = mqtt_url;
                    log_info("IoT DNS %s: %s", mqtt_dns_key, mqtt_url);
                }
            }
        } else if (strcmp(dns_resp.endpoints[i].key, "httpsUrl") == 0) {
            client->https_url = pal_strdup(client->pal, dns_resp.endpoints[i].addr);
            log_info("IoT DNS httpsUrl: %s", dns_resp.endpoints[i].addr);
        }
    }
    iot_dns_url_config_response_free(client->pal, &dns_resp);

    return OPRT_OK;
}

int iot_init(const pal_t *pal)
{
    if (!pal_is_valid(pal)) {
        log_error("iot_init: invalid PAL adapter (NULL or missing function pointers)");
        return OPRT_INVALID_PARAMETER;
    }
    set_pal(pal);
    /* Eager-seed the shared DRBG before any worker threads spawn. Every TLS
     * handshake (mqtt/http) and every AES-GCM nonce depends on it; rng_bytes()
     * fails closed if seeding failed, so surface the failure here rather than
     * letting it resurface later as opaque handshake / nonce errors. */
    if (rng_init(pal) != 0) {
        log_error("iot_init: RNG seed failed (no strong entropy source?)");
        return OPRT_COMMUNICATION_ERROR;
    }

    cJSON_Hooks hooks = { .malloc_fn = pal->malloc, .free_fn = pal->free };
    cJSON_InitHooks(&hooks);

    return OPRT_OK;
}

int iot_init_default(void)
{
    return iot_init(get_default_pal());
}

IOT_API iot_client_t *iot_client_init(const iot_client_config_t *config)
{
    if (!config) {
        log_error("Invalid config for iot_client_init");
        return NULL;
    }

    const pal_t *pal = get_pal();
    if (!pal) {
        log_error("iot_client_init: PAL not initialized — call iot_init() first");
        return NULL;
    }
    iot_client_t *client = (iot_client_t *)pal->malloc(sizeof(iot_client_t));
    if (client == NULL) {
        log_error("Failed to allocate iot_client_t");
        return NULL;
    }
    memset(client, 0, sizeof(iot_client_t));
    client->pal = pal;

    strncpy(client->devid, config->devid, sizeof(client->devid) - 1);
    client->devid[sizeof(client->devid) - 1] = '\0';
    strncpy(client->secret_key, config->secret_key, sizeof(client->secret_key) - 1);
    client->secret_key[sizeof(client->secret_key) - 1] = '\0';
    strncpy(client->local_key, config->local_key, sizeof(client->local_key) - 1);
    client->local_key[sizeof(client->local_key) - 1] = '\0';
    client->region = config->region;
    client->env = config->env;
    client->mqtt_disable_tls = config->mqtt_disable_tls;
    client->cacert = config->cacert;
    client->message_callback = config->message_callback;

    /* DP layer: restore persisted schema_id / schema from config (restart path).
     * On-boarding paths leave these NULL here and set them after activation. */
    if (config->schema_id && config->schema_id[0] != '\0') {
        strncpy(client->schema_id, config->schema_id, sizeof(client->schema_id) - 1);
        client->schema_id[sizeof(client->schema_id) - 1] = '\0';
    }
    if (config->schema && config->schema[0] != '\0') {
        client->schema = pal_strdup(pal, config->schema);
    }

    if (client->devid[0] != '\0') {
        iot_client_dns_resolve(client);
    }

    if (client->mqtt_url && config->mqtt_auto_connect) {
        int ret = iot_client_message_connect(client);
        if (ret != OPRT_OK) {
            log_error("MQTT connect failed: %d", ret);
            iot_client_deinit(client);
            return NULL;
        }
    }

    /* Report SDK version to cloud */
    {
        char meta_host[64] = {0};
        uint16_t meta_port = IOT_DEFAULT_PORT;
        const char *host;
        if (client->https_url) {
            parse_host_port(client->https_url, meta_host, sizeof(meta_host), &meta_port);
            host = meta_host;
        } else {
            host = iot_region_to_host(client->region, client->env);
        }
        device_meta_save_request_t meta_req = {
            .devid       = client->devid,
            .key         = client->secret_key,
            .sdk_version = SDK_VERSION,
            .host        = host,
            .port        = meta_port,
            .cacert      = client->cacert,
        };
        device_meta_save_response_t meta_resp = {0};
        int ret = atop_device_meta_save(pal, &meta_req, &meta_resp);
        if (ret != OPRT_OK) {
            log_warn("atop_device_meta_save failed: %d (non-fatal)", ret);
        }
    }

    /* Report firmware version to cloud (enables OTA upgrade checks) */
    {
        int ret = iot_ota_report_version(client, IOT_SDK_SW_VER);
        if (ret != OPRT_OK) {
            log_warn("iot_ota_report_version failed: %d (non-fatal)", ret);
        }
    }

    /* DP layer: build the registry from the (possibly restored) schema, then
     * restore persisted DP values without marking dirty / publishing. */
    iot_dp_rebuild(client);
    if (config->dp_state && config->dp_state[0] != '\0') {
        iot_dp_restore_json(client, config->dp_state);
    }

    return client;
}

IOT_API void iot_client_deinit(iot_client_t *client)
{
    if (client == NULL) {
        return;
    }
    iot_client_message_disconnect(client);
    iot_dp_deinit(client);   /* after disconnect: no more dispatch on the process thread */
    if (client->https_url) {
        client->pal->free(client->https_url);
    }
    if (client->mqtt_url) {
        client->pal->free(client->mqtt_url);
    }
    if (client->schema) {
        client->pal->free(client->schema);
    }
    const pal_t *pal = client->pal;
    pal->free(client);
}

IOT_API iot_client_t *iot_client_init_on_boarding(const iot_on_boarding_config_t *config)
{
    if (!config) {
        log_error("Invalid config for iot_client_init_on_boarding");
        return NULL;
    }

    const pal_t *pal = get_pal();
    if (!pal) {
        log_error("iot_client_init_on_boarding: PAL not initialized — call iot_init() first");
        return NULL;
    }

    if (config->uuid[0] == '\0') {
        log_error("uuid is required for on boarding");
        return NULL;
    }

    log_info("iot_client_init_on_boarding: uuid=%s", config->uuid);

    /* Build the internal on_boarding_config_t from the public config */
    on_boarding_config_t ob_cfg = {0};
    strncpy(ob_cfg.uuid, config->uuid, sizeof(ob_cfg.uuid) - 1);
    strncpy(ob_cfg.authkey, config->authkey, sizeof(ob_cfg.authkey) - 1);
    strncpy(ob_cfg.product_key, config->product_key, sizeof(ob_cfg.product_key) - 1);

    strncpy(ob_cfg.sw_ver, IOT_SDK_SW_VER, sizeof(ob_cfg.sw_ver) - 1);
    strncpy(ob_cfg.pv, IOT_SDK_PV, sizeof(ob_cfg.pv) - 1);
    strncpy(ob_cfg.bv, IOT_SDK_BV, sizeof(ob_cfg.bv) - 1);

    ob_cfg.modules = config->modules;
    ob_cfg.feature = config->feature;
    ob_cfg.skill_param = config->skill_param;
    if (config->firmware_key[0] != '\0')
        strncpy(ob_cfg.firmware_key, config->firmware_key, sizeof(ob_cfg.firmware_key) - 1);

    ob_cfg.timeout_ms = config->timeout_ms;
    ob_cfg.env = config->env;
    ob_cfg.mqtt_disable_tls = config->mqtt_disable_tls;
    ob_cfg.cacert = config->cacert;
    ob_cfg.dns_host = NULL;
    ob_cfg.dns_port = 0;

    /* Wait for QR code activation */
    on_boarding_response_t ob_resp = {0};
    int ret = on_boarding_with_qrcode(pal, &ob_cfg, &ob_resp);
    if (ret != OPRT_OK) {
        log_error("on_boarding_with_qrcode failed: %d", ret);
        return NULL;
    }

    log_info("On-boarding successful, initializing client with activated credentials");

    /* Build iot_client_config_t from activation results and call iot_client_init */
    iot_client_config_t client_config = {0};
    strncpy(client_config.devid, ob_resp.devid, sizeof(client_config.devid) - 1);
    strncpy(client_config.secret_key, ob_resp.secret_key, sizeof(client_config.secret_key) - 1);
    strncpy(client_config.local_key, ob_resp.local_key, sizeof(client_config.local_key) - 1);
    client_config.region = ob_resp.region;
    client_config.env = ob_resp.env;
    client_config.mqtt_disable_tls = config->mqtt_disable_tls;
    client_config.mqtt_auto_connect = config->mqtt_auto_connect;
    client_config.cacert = config->cacert;
    client_config.message_callback = config->message_callback;
    /* schema / schema_id come from the activation response. iot_client_init()
     * copies them and rebuilds the DP registry from the schema (dp_state is then
     * initialized from the schema); first activation has no persisted DP values. */
    client_config.schema    = ob_resp.schema;
    client_config.schema_id = ob_resp.schema_id;
    client_config.dp_state  = NULL;

    iot_client_t *client = iot_client_init(&client_config);
    pal->free(ob_resp.schema);   /* iot_client_init copied it (or failed) */
    if (client == NULL) {
        log_error("iot_client_init failed after on-boarding");
        return NULL;
    }

    /* First activation: seed schema-derived default DP values (marked dirty) so
     * the app can report a complete initial state. */
    iot_dp_init_defaults(client);

    return client;
}

IOT_API iot_client_t *iot_client_init_on_boarding_with_token(const iot_on_boarding_config_t *config, const char *token)
{
    if (!config) {
        log_error("Invalid config for iot_client_init_on_boarding_with_token");
        return NULL;
    }

    const pal_t *pal = get_pal();
    if (!pal) {
        log_error("iot_client_init_on_boarding_with_token: PAL not initialized — call iot_init() first");
        return NULL;
    }

    if (config->uuid[0] == '\0') {
        log_error("uuid is required for on boarding with token");
        return NULL;
    }

    if (!token || token[0] == '\0') {
        log_error("token is required for on boarding with token");
        return NULL;
    }

    log_info("iot_client_init_on_boarding_with_token: uuid=%s", config->uuid);

    on_boarding_config_t ob_cfg = {0};
    strncpy(ob_cfg.uuid, config->uuid, sizeof(ob_cfg.uuid) - 1);
    strncpy(ob_cfg.authkey, config->authkey, sizeof(ob_cfg.authkey) - 1);
    strncpy(ob_cfg.product_key, config->product_key, sizeof(ob_cfg.product_key) - 1);

    strncpy(ob_cfg.sw_ver, IOT_SDK_SW_VER, sizeof(ob_cfg.sw_ver) - 1);
    strncpy(ob_cfg.pv, IOT_SDK_PV, sizeof(ob_cfg.pv) - 1);
    strncpy(ob_cfg.bv, IOT_SDK_BV, sizeof(ob_cfg.bv) - 1);

    ob_cfg.modules = config->modules;
    ob_cfg.feature = config->feature;
    ob_cfg.skill_param = config->skill_param;
    if (config->firmware_key[0] != '\0')
        strncpy(ob_cfg.firmware_key, config->firmware_key, sizeof(ob_cfg.firmware_key) - 1);

    ob_cfg.env = config->env;
    ob_cfg.mqtt_disable_tls = config->mqtt_disable_tls;
    ob_cfg.cacert = config->cacert;
    ob_cfg.dns_host = NULL;
    ob_cfg.dns_port = 0;

    on_boarding_response_t ob_resp = {0};
    int ret = on_boarding_with_token(pal, &ob_cfg, token, &ob_resp);
    if (ret != OPRT_OK) {
        log_error("on_boarding_with_token failed: %d", ret);
        return NULL;
    }

    log_info("On-boarding with token successful, initializing client with activated credentials");

    iot_client_config_t client_config = {0};
    strncpy(client_config.devid, ob_resp.devid, sizeof(client_config.devid) - 1);
    strncpy(client_config.secret_key, ob_resp.secret_key, sizeof(client_config.secret_key) - 1);
    strncpy(client_config.local_key, ob_resp.local_key, sizeof(client_config.local_key) - 1);
    client_config.region = ob_resp.region;
    client_config.env = ob_resp.env;
    client_config.mqtt_disable_tls = config->mqtt_disable_tls;
    client_config.mqtt_auto_connect = config->mqtt_auto_connect;
    client_config.cacert = config->cacert;
    client_config.message_callback = config->message_callback;
    /* schema / schema_id come from the activation response. iot_client_init()
     * copies them and rebuilds the DP registry from the schema (dp_state is then
     * initialized from the schema); first activation has no persisted DP values. */
    client_config.schema    = ob_resp.schema;
    client_config.schema_id = ob_resp.schema_id;
    client_config.dp_state  = NULL;

    iot_client_t *client = iot_client_init(&client_config);
    pal->free(ob_resp.schema);   /* iot_client_init copied it (or failed) */
    if (client == NULL) {
        log_error("iot_client_init failed after on-boarding with token");
        return NULL;
    }

    /* First activation: seed schema-derived default DP values (marked dirty) so
     * the app can report a complete initial state. */
    iot_dp_init_defaults(client);

    return client;
}

IOT_API int iot_client_get_session_token(iot_client_t *client, const char *agent_code, char *token, size_t token_len)
{
    if (client == NULL || token == NULL || token_len == 0) {
        log_error("iot_client_get_session_token: invalid parameters");
        return OPRT_INVALID_PARAMETER;
    }

    char parsed_host[64] = {0};
    uint16_t parsed_port = IOT_DEFAULT_PORT;
    iot_client_resolve_atop_host(client, parsed_host, sizeof(parsed_host), &parsed_port);
    const char *host = parsed_host[0] ? parsed_host : NULL;
    ai_token_request_t req = {
        .devid = client->devid,
        .key = client->secret_key,
        .agent_code = agent_code,
        .host = host,
        .port = parsed_port,
        .cacert = client->cacert,
    };

    ai_token_response_t resp = {0};
    int ret = atop_ai_token_get(client->pal, &req, &resp);
    if (ret != OPRT_OK) {
        log_error("atop_ai_token_get failed: %d", ret);
        return ret;
    }
    size_t resp_token_len = strlen(resp.token);
    if (resp_token_len >= token_len) {
        log_error("token buffer too small: need %zu, have %zu", resp_token_len + 1, token_len);
        client->pal->free(resp.token);
        return OPRT_INVALID_RESULT;
    }
    memcpy(token, resp.token, resp_token_len);
    token[resp_token_len] = '\0';
    client->pal->free(resp.token);
    return OPRT_OK;
}

IOT_API int iot_client_process(iot_client_t *client, uint32_t timeout_ms)
{
    if (client == NULL) {
        return OPRT_INVALID_PARAMETER;
    }
    return iot_client_message_process(client, timeout_ms);
}

IOT_API int iot_client_publish(iot_client_t *client, const uint8_t *data, size_t data_len)
{
    if (client == NULL) {
        return OPRT_INVALID_PARAMETER;
    }
    return iot_client_message_publish(client, data, data_len);
}

IOT_API int iot_get_ca_certificate(iot_client_t *client, const char *host, uint16_t port, char **ca_certificate)
{
    if (client == NULL || host == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    const pal_t *pal = client->pal;
    iot_dns_ca_cert_request_t req = {
        .host = IOT_DNS_DEFAULT_HOST,
        .port = IOT_DNS_DEFAULT_PORT,
        .cacert = client->cacert,
        .target_host = host,
        .target_port = port,
        .public_key_algorithm = "ECDSA",
    };

    iot_dns_ca_cert_response_t resp = {0};
    int ret = iot_dns_get_ca_cert(pal, &req, &resp);
    if (ret != OPRT_OK) {
        log_error("iot_dns_get_ca_cert failed: %d", ret);
        return ret;
    }
    *ca_certificate = NULL;
    if (resp.ca_certificate) {
        *ca_certificate = pal_strdup(pal, resp.ca_certificate);
    }
    iot_dns_ca_cert_response_free(pal, &resp);
    if (*ca_certificate == NULL) {
        return OPRT_INVALID_RESULT;
    }
    return OPRT_OK;
}

IOT_API int iot_get_qrcode_info(const iot_qrcode_request_t *request, iot_qrcode_response_t *response)
{
    const pal_t *pal = get_pal();
    if (!pal) {
        log_error("iot_get_qrcode_info: PAL not initialized — call iot_init() first");
        return OPRT_UNINITIALIZED;
    }

    if (request == NULL || response == NULL) {
        return OPRT_INVALID_PARAMETER;
    }

    const char *region_str = iot_region_to_string(request->region);
    if (region_str == NULL) {
        region_str = "AY";
    }

    iot_dns_config_item_t dns_keys[] = {
        { .key = "httpsUrl" },
    };
    iot_dns_url_config_request_t dns_req = {
        .cacert       = request->cacert,
        .host         = NULL,
        .port         = 0,
        .region       = region_str,
        .env          = iot_env_to_string(request->env),
        .uuid         = request->uuid,
        .config       = dns_keys,
        .config_count = 1,
    };
    iot_dns_url_config_response_t dns_resp = {0};
    int ret = iot_dns_url_config(pal, &dns_req, &dns_resp);
    if (ret != OPRT_OK) {
        log_error("iot_get_qrcode_info: iot_dns_url_config failed: %d", ret);
        return ret;
    }

    char host[64] = {0};
    uint16_t port = IOT_DEFAULT_PORT;
    for (int i = 0; i < dns_resp.endpoint_count; i++) {
        if (strcmp(dns_resp.endpoints[i].key, "httpsUrl") == 0) {
            parse_host_port(dns_resp.endpoints[i].addr, host, sizeof(host), &port);
            break;
        }
    }
    iot_dns_url_config_response_free(pal, &dns_resp);

    if (host[0] == '\0') {
        log_error("iot_get_qrcode_info: httpsUrl not found in DNS response");
        return OPRT_COMMUNICATION_ERROR;
    }

    qrcode_info_request_t req = {
        .uuid    = request->uuid,
        .authkey = request->authkey,
        .app_id  = request->app_id,
        .type    = request->type,
        .host    = host,
        .port    = port,
        .cacert  = request->cacert,
    };

    qrcode_info_response_t resp = {0};
    ret = atop_qrcode_info_get(pal, &req, &resp);
    if (ret != OPRT_OK) {
        return ret;
    }

    response->url = resp.short_url;
    return OPRT_OK;
}
