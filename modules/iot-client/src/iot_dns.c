#include "iot_dns.h"
#include "http_client_interface.h"
#include "iot_config_defaults.h"
#include "cipher_wrapper.h"

#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static void parse_ip_array(cJSON *arr, char out[][64], int *count, int max) {
    *count = 0;
    if (!arr || !cJSON_IsArray(arr)) return;
    int n = cJSON_GetArraySize(arr);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(out[*count], item->valuestring, 63);
            out[*count][63] = '\0';
            (*count)++;
        }
    }
}

static int dns_http_request(const pal_t *pal, const char *host, uint16_t port, const char *cacert,
                            const char *method, const char *path,
                            const char *body, cJSON **out_json) {
    http_client_header_t headers[] = {
        {.key = "Content-Type", .value = "application/json"},
    };
    int has_body = (body && body[0] != '\0');

    http_client_response_t http_resp = {0};
    http_client_status_t status = http_client_request(
        &(const http_client_request_t){
            .cacert = cacert,
            .host = host,
            .port = port,
            .method = method,
            .path = path,
            .headers = headers,
            .headers_count = 1,
            .body = has_body ? (const uint8_t *)body : NULL,
            .body_length = has_body ? strlen(body) : 0,
            .timeout_ms = IOT_HTTP_TIMEOUT_MS_DEFAULT,
            .pal = pal,
        },
        &http_resp);

    if (status != HTTP_CLIENT_SUCCESS) {
        log_error("iot_dns: %s %s failed: %d", method, path, status);
        return OPRT_COMMUNICATION_ERROR;
    }

    if (!http_resp.body || http_resp.body_length == 0) {
        log_error("iot_dns: empty response from %s %s", method, path);
        http_client_free(pal, &http_resp);
        return OPRT_COMMUNICATION_ERROR;
    }

    char *body_str = pal->malloc(http_resp.body_length + 1);
    if (!body_str) {
        http_client_free(pal, &http_resp);
        return OPRT_MALLOC_FAILED;
    }
    memcpy(body_str, http_resp.body, http_resp.body_length);
    body_str[http_resp.body_length] = '\0';
    http_client_free(pal, &http_resp);

    *out_json = cJSON_Parse(body_str);
    pal->free(body_str);
    if (!*out_json) {
        log_error("iot_dns: failed to parse JSON response from %s %s", method, path);
        return OPRT_COMMUNICATION_ERROR;
    }
    return OPRT_OK;
}

/* ============================================================================
 * v1/dns_query
 * ============================================================================ */

int iot_dns_query(const pal_t *pal, const iot_dns_query_request_t *request,
                  iot_dns_query_response_t *response) {
    if (!request || !response || !request->domains || request->domain_count <= 0) {
        return OPRT_INVALID_PARAMETER;
    }

    memset(response, 0, sizeof(*response));

    const char *host = request->host ? request->host : IOT_DNS_DEFAULT_HOST;
    uint16_t port = request->port > 0 ? request->port : IOT_DNS_DEFAULT_PORT;

    cJSON *req_arr = cJSON_CreateArray();
    if (!req_arr) return OPRT_MALLOC_FAILED;

    for (int i = 0; i < request->domain_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "domain", request->domains[i].domain);
        if (request->domains[i].need_ip6) {
            cJSON_AddTrueToObject(item, "need_ip6");
        }
        cJSON_AddItemToArray(req_arr, item);
    }

    char *json_body = cJSON_PrintUnformatted(req_arr);
    cJSON_Delete(req_arr);
    if (!json_body) return OPRT_MALLOC_FAILED;

    cJSON *root = NULL;
    int ret = dns_http_request(pal, host, port, request->cacert,
                              "POST", "/v1/dns_query", json_body, &root);
    cJSON_free(json_body);
    if (ret != OPRT_OK) return ret;

    response->results = pal->malloc(
        sizeof(iot_dns_domain_result_t) * request->domain_count);
    if (!response->results) {
        cJSON_Delete(root);
        return OPRT_MALLOC_FAILED;
    }
    memset(response->results, 0,
           sizeof(iot_dns_domain_result_t) * request->domain_count);

    for (int i = 0; i < request->domain_count; i++) {
        const char *domain = request->domains[i].domain;
        cJSON *entry = cJSON_GetObjectItem(root, domain);
        if (!entry) continue;

        iot_dns_domain_result_t *r = &response->results[response->result_count];
        r->domain = pal_strdup(pal, domain);

        parse_ip_array(cJSON_GetObjectItem(entry, "Ips"),
                       r->ips, &r->ip_count, IOT_DNS_MAX_IPS);
        parse_ip_array(cJSON_GetObjectItem(entry, "ip6s"),
                       r->ip6s, &r->ip6_count, IOT_DNS_MAX_IPS);

        cJSON *ttl = cJSON_GetObjectItem(entry, "ttl");
        if (ttl && cJSON_IsNumber(ttl)) r->ttl = ttl->valueint;

        response->result_count++;
    }

    cJSON_Delete(root);
    return OPRT_OK;
}

void iot_dns_query_response_free(const pal_t *pal, iot_dns_query_response_t *response) {
    if (!response) return;
    if (response->results) {
        for (int i = 0; i < response->result_count; i++) {
            pal->free(response->results[i].domain);
        }
        pal->free(response->results);
        response->results = NULL;
    }
    response->result_count = 0;
}

/* ============================================================================
 * v2/url_config
 * ============================================================================ */

int iot_dns_url_config(const pal_t *pal, const iot_dns_url_config_request_t *request,
                       iot_dns_url_config_response_t *response) {
    if (!request || !response) return OPRT_INVALID_PARAMETER;
    if (!request->env || !request->uuid) return OPRT_INVALID_PARAMETER;
    if (!request->config || request->config_count <= 0) return OPRT_INVALID_PARAMETER;

    memset(response, 0, sizeof(*response));

    const char *host = request->host ? request->host : IOT_DNS_DEFAULT_HOST;
    uint16_t port = request->port > 0 ? request->port : IOT_DNS_DEFAULT_PORT;

    cJSON *req_obj = cJSON_CreateObject();
    if (!req_obj) return OPRT_MALLOC_FAILED;

    if (request->region) cJSON_AddStringToObject(req_obj, "region", request->region);
    cJSON_AddStringToObject(req_obj, "env", request->env);
    cJSON_AddStringToObject(req_obj, "uuid", request->uuid);

    cJSON *cfg_arr = cJSON_AddArrayToObject(req_obj, "config");
    for (int i = 0; i < request->config_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", request->config[i].key);
        if (request->config[i].need_ip6)
            cJSON_AddTrueToObject(item, "need_ip6");
        if (request->config[i].need_ca)
            cJSON_AddTrueToObject(item, "need_ca");
        cJSON_AddItemToArray(cfg_arr, item);
    }

    char *json_body = cJSON_PrintUnformatted(req_obj);
    cJSON_Delete(req_obj);
    if (!json_body) return OPRT_MALLOC_FAILED;

    cJSON *root = NULL;
    int ret = dns_http_request(pal, host, port, request->cacert,
                              "POST", "/v2/url_config", json_body, &root);
    cJSON_free(json_body);
    if (ret != OPRT_OK) return ret;

    /* ttl */
    cJSON *ttl_item = cJSON_GetObjectItem(root, "ttl");
    if (ttl_item && cJSON_IsNumber(ttl_item))
        response->ttl = ttl_item->valueint;

    /* caArr */
    cJSON *ca_arr = cJSON_GetObjectItem(root, "caArr");
    if (ca_arr && cJSON_IsArray(ca_arr)) {
        int n = cJSON_GetArraySize(ca_arr);
        if (n > 0) {
            response->ca_arr = pal->malloc(sizeof(char *) * n);
            if (!response->ca_arr) {
                cJSON_Delete(root);
                return OPRT_MALLOC_FAILED;
            }
            response->ca_count = 0;
            for (int i = 0; i < n; i++) {
                cJSON *cert = cJSON_GetArrayItem(ca_arr, i);
                if (cJSON_IsString(cert) && cert->valuestring) {
                    response->ca_arr[response->ca_count] = pal_strdup(pal, cert->valuestring);
                    if (response->ca_arr[response->ca_count])
                        response->ca_count++;
                }
            }
        }
    }

    /* endpoints — iterate requested keys */
    int max_ep = request->config_count;
    if (max_ep > IOT_DNS_MAX_ENDPOINTS) max_ep = IOT_DNS_MAX_ENDPOINTS;

    response->endpoints = pal->malloc(sizeof(iot_dns_endpoint_t) * max_ep);
    if (!response->endpoints) {
        cJSON_Delete(root);
        iot_dns_url_config_response_free(pal, response);
        return OPRT_MALLOC_FAILED;
    }
    memset(response->endpoints, 0, sizeof(iot_dns_endpoint_t) * max_ep);
    response->endpoint_count = 0;

    for (int i = 0; i < max_ep; i++) {
        const char *key = request->config[i].key;
        cJSON *ep = cJSON_GetObjectItem(root, key);
        if (!ep || !cJSON_IsObject(ep)) continue;

        iot_dns_endpoint_t *e = &response->endpoints[response->endpoint_count];
        strncpy(e->key, key, sizeof(e->key) - 1);

        cJSON *addr = cJSON_GetObjectItem(ep, "addr");
        if (addr && cJSON_IsString(addr))
            e->addr = pal_strdup(pal, addr->valuestring);

        parse_ip_array(cJSON_GetObjectItem(ep, "ips"),
                       e->ips, &e->ip_count, IOT_DNS_MAX_IPS);
        parse_ip_array(cJSON_GetObjectItem(ep, "ip6s"),
                       e->ip6s, &e->ip6_count, IOT_DNS_MAX_IPS);

        response->endpoint_count++;
    }

    cJSON_Delete(root);
    return OPRT_OK;
}

void iot_dns_url_config_response_free(const pal_t *pal, iot_dns_url_config_response_t *response) {
    if (!response) return;
    if (response->ca_arr) {
        for (int i = 0; i < response->ca_count; i++) {
            if (response->ca_arr[i]) pal->free(response->ca_arr[i]);
        }
        pal->free(response->ca_arr);
        response->ca_arr = NULL;
    }
    response->ca_count = 0;
    if (response->endpoints) {
        for (int i = 0; i < response->endpoint_count; i++) {
            pal->free(response->endpoints[i].addr);
        }
        pal->free(response->endpoints);
        response->endpoints = NULL;
    }
    response->endpoint_count = 0;
    response->ttl = 0;
}

/* ============================================================================
 * GET /api/v1/ca-certificate
 * ============================================================================ */

int iot_dns_get_ca_cert(const pal_t *pal, const iot_dns_ca_cert_request_t *request,
                        iot_dns_ca_cert_response_t *response) {
    if (!request || !response || !request->target_host || request->target_host[0] == '\0') {
        return OPRT_INVALID_PARAMETER;
    }

    memset(response, 0, sizeof(*response));

    const char *dns_host = request->host ? request->host : IOT_DNS_DEFAULT_HOST;
    uint16_t dns_port = request->port > 0 ? request->port : IOT_DNS_DEFAULT_PORT;
    uint16_t target_port = request->target_port > 0 ? request->target_port : 443;
    const char *algo = request->public_key_algorithm ? request->public_key_algorithm : "RSA";

    size_t host_len = strlen(request->target_host);
    size_t algo_len = strlen(algo);
    size_t path_len = 62 + host_len + algo_len;
    char *path = (char *)pal->malloc(path_len);
    if (!path) return OPRT_MALLOC_FAILED;
    int sn = snprintf(path, path_len,
             "/api/v1/ca-certificate?host=%s&port=%u&public_key_algorithm=%s",
             request->target_host, target_port, algo);
    if (sn < 0 || (size_t)sn >= path_len) {
        pal->free(path);
        return OPRT_COMMUNICATION_ERROR;
    }

    cJSON *root = NULL;
    int ret = dns_http_request(pal, dns_host, dns_port, request->cacert,
                               "GET", path, NULL, &root);
    pal->free(path);
    if (ret != OPRT_OK) return ret;

    cJSON *cert = cJSON_GetObjectItem(root, "ca_certificate");
    if (cert && cJSON_IsString(cert) && cert->valuestring) {
        response->ca_certificate = pal_strdup(pal, cert->valuestring);
    } else {
        response->ca_certificate = pal_strdup(pal, "");
    }

    cJSON_Delete(root);

    if (!response->ca_certificate) return OPRT_MALLOC_FAILED;
    return OPRT_OK;
}

void iot_dns_ca_cert_response_free(const pal_t *pal, iot_dns_ca_cert_response_t *response) {
    if (!response) return;
    if (response->ca_certificate) {
        pal->free(response->ca_certificate);
        response->ca_certificate = NULL;
    }
}

char *iot_region_to_host(iot_region_t region, iot_env_t env)
{
    switch (env) {
        case PRE:
                switch (region) {
                    case AY:
                        return IOT_CN_PRE_HOST;
                    case US:
                        return IOT_US_PRE_HOST;
                    case UEAZ:
                        return IOT_UEAZ_PRE_HOST;
                    case EU:
                        return IOT_EU_PRE_HOST;
                    case WEAZ:
                        return IOT_WEAZ_PRE_HOST;
                    case IN:
                        return IOT_IN_PRE_HOST;
                    default:
                        return IOT_DEFAULT_PRE_HOST;
                }
        case PROD:
            switch (region) {
                case AY:
                    return IOT_CN_HOST;
                case US:
                    return IOT_US_HOST;
                case UEAZ:
                    return IOT_UEAZ_HOST;
                case EU:
                    return IOT_EU_HOST;
                case WEAZ:
                    return IOT_WEAZ_HOST;
                case IN:
                    return IOT_IN_HOST;
                default:
                    return IOT_DEFAULT_HOST;
                }
        case TEST:
            return IOT_TEST_HOST;
        default:
            return IOT_DEFAULT_HOST;
    }
}
