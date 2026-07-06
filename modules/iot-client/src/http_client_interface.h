#ifndef __HTTP_CLIENT_INTERFACE_H__
#define __HTTP_CLIENT_INTERFACE_H__

#include <stdint.h>
#include <stddef.h>
#include "iot_client.h"
#include "tls.h"

typedef enum {
    HTTP_CLIENT_SUCCESS = 0,
    HTTP_CLIENT_ERROR = -1,
    HTTP_CLIENT_TIMEOUT = -2,
    HTTP_CLIENT_INVALID_PARAM = -3,
    HTTP_CLIENT_TLS_ERROR = -4
} http_client_status_t;

typedef struct {
    const char *key;
    const char *value;
} http_client_header_t;

typedef struct {
    const char *cacert;        // CA certificate PEM content for TLS verification
    tls_cert_bundle_attach_fn cert_bundle_attach; // Platform cert-bundle callback (NULL = none)
    const char *host;
    uint16_t port;
    const char *method;
    const char *path;
    const http_client_header_t *headers;
    uint8_t headers_count;
    const uint8_t *body;
    size_t body_length;
    uint32_t timeout_ms;
    const pal_t *pal;
} http_client_request_t;

typedef struct {
    uint8_t *body;
    size_t body_length;
    int status_code;
    void *internal;  // Platform-specific data
} http_client_response_t;

/**
 * @brief Send HTTP request
 *
 * @param request HTTP request parameters
 * @param response HTTP response structure (caller must free body if allocated)
 * @return http_client_status_t HTTP_CLIENT_SUCCESS on success
 */
http_client_status_t http_client_request(const http_client_request_t *request,
                                        http_client_response_t *response);

/**
 * @brief Free HTTP response resources
 *
 * @param response Response structure to free
 */
void http_client_free(const pal_t *pal, http_client_response_t *response);

#endif /* __HTTP_CLIENT_INTERFACE_H__ */

