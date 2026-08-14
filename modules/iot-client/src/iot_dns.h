#ifndef __IOT_DNS_H__
#define __IOT_DNS_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iot_config_defaults.h"
#include "tls.h"

#define IOT_DNS_DEFAULT_HOST "h1.iot-dns.com"
#define IOT_DNS_DEFAULT_PORT 443

#define IOT_DNS_MAX_IPS      8
#define IOT_DNS_MAX_ENDPOINTS 20
#define IOT_DNS_MAX_CAS      4

/* Endpoint keys the SDK asks POST /v2/url_config for. Named here because three
 * call sites request them (iot_client_dns_resolve, iot_get_qrcode_info and the
 * activation path in iot_on_boarding.c) and each one has to compare the key back
 * against the response — a literal drifting in one of them resolves nothing, and
 * the service reports that as HTTP 200 with the endpoint simply absent.
 *
 * These are the m1/a generation. The service also publishes an mqttsStdUrl /
 * httpsStdUrl pair (m6/a6 hosts, MQTT on 8886, EC service CA rather than RSA);
 * switching to it is a deployment decision, not a drop-in — it changes the broker
 * port and makes iot_get_ca_certificate() succeed, which turns peer verification
 * on for the first time. */
#define IOT_DNS_KEY_MQTTS  "mqttsUrl"
#define IOT_DNS_KEY_MQTT   "mqttUrl"
#define IOT_DNS_KEY_HTTPS  "httpsUrl"

/* ============================================================================
 * v1/dns_query
 * ============================================================================ */

typedef struct {
    const char *domain;
    bool need_ip6;
} iot_dns_domain_t;

typedef struct {
    char *domain;
    char ips[IOT_DNS_MAX_IPS][64];
    int ip_count;
    char ip6s[IOT_DNS_MAX_IPS][64];
    int ip6_count;
    int ttl;
} iot_dns_domain_result_t;

typedef struct {
    const char *host;
    uint16_t port;
    const char *cacert;
    tls_cert_bundle_attach_fn cert_bundle_attach;
    const iot_dns_domain_t *domains;
    int domain_count;
} iot_dns_query_request_t;

typedef struct {
    iot_dns_domain_result_t *results;
    int result_count;
} iot_dns_query_response_t;

/**
 * @brief Query DNS records for one or more domains (v1/dns_query).
 *
 * @param request  Request containing the DNS server and domain list.
 * @param response Caller-provided response struct; populated on success.
 * @return 0 on success, negative error code on failure.
 */
int iot_dns_query(const pal_t *pal, const iot_dns_query_request_t *request,
                  iot_dns_query_response_t *response);

void iot_dns_query_response_free(const pal_t *pal, iot_dns_query_response_t *response);

/* ============================================================================
 * v2/url_config
 * ============================================================================ */

typedef struct {
    const char *key;
    bool need_ip6;
    bool need_ca;
} iot_dns_config_item_t;

typedef struct {
    char key[64];
    char *addr;
    char ips[IOT_DNS_MAX_IPS][64];
    int ip_count;
    char ip6s[IOT_DNS_MAX_IPS][64];
    int ip6_count;
} iot_dns_endpoint_t;

typedef struct {
    const char *host;
    uint16_t port;
    const char *cacert;
    tls_cert_bundle_attach_fn cert_bundle_attach;
    const char *region;
    const char *env;
    const char *uuid;
    const iot_dns_config_item_t *config;
    int config_count;
} iot_dns_url_config_request_t;

typedef struct {
    char **ca_arr;
    int ca_count;
    int ttl;
    iot_dns_endpoint_t *endpoints;
    int endpoint_count;
} iot_dns_url_config_response_t;

/**
 * @brief Query service endpoint URLs (v2/url_config).
 *
 * @param request  Request containing the DNS server, env, uuid, and service keys.
 * @param response Caller-provided response struct; populated on success.
 * @return 0 on success, negative error code on failure.
 */
int iot_dns_url_config(const pal_t *pal, const iot_dns_url_config_request_t *request,
                       iot_dns_url_config_response_t *response);

void iot_dns_url_config_response_free(const pal_t *pal, iot_dns_url_config_response_t *response);

/* ============================================================================
 * GET /api/v1/ca-certificate
 * ============================================================================ */

typedef struct {
    const char *host;               // DNS service host (NULL = IOT_DNS_DEFAULT_HOST)
    uint16_t port;                  // DNS service port (0 = IOT_DNS_DEFAULT_PORT)
    const char *cacert;             // CA cert for TLS to DNS service
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback
    const char *target_host;        // Host to query CA certificate for (required)
    uint16_t target_port;           // Target service port (0 = 443)
    const char *public_key_algorithm; // "RSA" or "ECDSA" (NULL = "RSA")
} iot_dns_ca_cert_request_t;

typedef struct {
    char *ca_certificate;           // Dynamically allocated; empty string if not found
} iot_dns_ca_cert_response_t;

/**
 * @brief Query CA certificate for a given host (GET /api/v1/ca-certificate).
 *
 * @param request  Request with target host and optional port / algorithm.
 * @param response Caller-provided response struct; populated on success.
 * @return 0 on success, negative error code on failure.
 */
int iot_dns_get_ca_cert(const pal_t *pal, const iot_dns_ca_cert_request_t *request,
                        iot_dns_ca_cert_response_t *response);

void iot_dns_ca_cert_response_free(const pal_t *pal, iot_dns_ca_cert_response_t *response);

char *iot_region_to_host(iot_region_t region, iot_env_t env);

/**
 * @brief Region code as the IoT DNS service spells it (the `region` field of
 *        POST /v2/url_config).
 *
 * These are the SAME two-letter codes that prefix an activation token, so this
 * is the exact inverse of __token_to_region() in iot_on_boarding.c — keep the
 * two in step. Note they are NOT the enum's own spelling: UEAZ is "UE" and WEAZ
 * is "WE" on the wire.
 *
 * @return the code, or NULL for an unknown region.
 */
const char *iot_region_to_string(iot_region_t region);

#endif /* __IOT_DNS_H__ */
