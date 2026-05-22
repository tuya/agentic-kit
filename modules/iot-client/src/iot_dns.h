#ifndef __IOT_DNS_H__
#define __IOT_DNS_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iot_config_defaults.h"

#define IOT_DNS_DEFAULT_HOST "h1.iot-dns.com"
#define IOT_DNS_DEFAULT_PORT 443

#define IOT_DNS_MAX_IPS      8
#define IOT_DNS_MAX_ENDPOINTS 20
#define IOT_DNS_MAX_CAS      4

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

#endif /* __IOT_DNS_H__ */
