#include "http_client_interface.h"
#include "core_http_client.h"
#include "transport_interface.h"
#include "iot_config_defaults.h"

#include <string.h>
#include <stdio.h>

// Shared TLS transport (mbedTLS lives entirely inside common/tls).
#include "tls.h"

// Network context structure definition with TLS support
struct HTTPNetworkContext {
    void *tcp_handle;
    bool use_tls;
    tls_t *tls;
    const char *host;
    uint16_t port;
    uint32_t timeout_ms;
    const pal_t *pal;
};

// Transport interface send function (supports both TCP and TLS)
static int32_t transport_send(NetworkContext_t *pNetworkContext,
                              const void *pBuffer,
                              size_t bytesToSend) {
    if (!pNetworkContext) {
        return OPRT_COMMUNICATION_ERROR;
    }

    struct HTTPNetworkContext *ctx = (struct HTTPNetworkContext *)pNetworkContext;

    if (ctx->use_tls) {
        int r = tls_write(ctx->tls, (const uint8_t *)pBuffer,
                          bytesToSend, 30000 /* 30s */);
        if (r != TLS_OK) {
            log_error("TLS write error");
            return OPRT_COMMUNICATION_ERROR;
        }
        return (int32_t)bytesToSend;
    } else {
        if (!ctx->tcp_handle) {
            return OPRT_COMMUNICATION_ERROR;
        }
        int bytes_sent = ctx->pal->tcp_send(ctx->tcp_handle,
                                            (const uint8_t *)pBuffer, bytesToSend,
                                            ctx->timeout_ms);
        if (bytes_sent == PAL_ERR_AGAIN) {
            return 0;
        }
        if (bytes_sent < 0) {
            log_error("TCP send error");
            return OPRT_COMMUNICATION_ERROR;
        }
        return (int32_t)bytes_sent;
    }
}

// Transport interface receive function (supports both TCP and TLS)
static int32_t transport_recv(NetworkContext_t *pNetworkContext,
                              void *pBuffer,
                              size_t bytesToRecv) {
    if (!pNetworkContext) {
        return OPRT_COMMUNICATION_ERROR;
    }

    struct HTTPNetworkContext *ctx = (struct HTTPNetworkContext *)pNetworkContext;

    if (ctx->use_tls) {
        int n = tls_read(ctx->tls, (uint8_t *)pBuffer, bytesToRecv, ctx->timeout_ms);
        if (n == TLS_ERR_AGAIN) {
            return 0;   // no data within timeout: coreHTTP polls again
        }
        if (n <= 0) {
            // n<0: TLS error. n==0: graceful peer close_notify — tls_read maps
            // no-data to TLS_ERR_AGAIN, so 0 here always means the peer closed
            // (e.g. server closing the connection mid-response). Surface it as an
            // error so the receive loop fails fast instead of spinning to timeout.
            log_error("TLS read error/closed (n=%d)", n);
            return OPRT_COMMUNICATION_ERROR;
        }
        return n;   // >0 bytes
    } else {
        if (!ctx->tcp_handle) {
            return OPRT_COMMUNICATION_ERROR;
        }
        int bytes_received = ctx->pal->tcp_recv(ctx->tcp_handle,
                                                (uint8_t *)pBuffer, bytesToRecv,
                                                ctx->timeout_ms);
        if (bytes_received == PAL_ERR_AGAIN) {
            return 0;
        }
        if (bytes_received < 0) {
            log_error("TCP recv error");
            return OPRT_COMMUNICATION_ERROR;
        }
        return (int32_t)bytes_received;
    }
}

// Establish TCP connection
static void *connect_tcp(const pal_t *pal, const char *host, uint16_t port, uint32_t timeout_ms) {
    void *handle = pal->tcp_connect(host, port, timeout_ms);
    if (!handle) {
        log_error("Failed to connect to %s:%d", host, port);
        return NULL;
    }
    log_debug("TCP connection established to %s:%d", host, port);
    return handle;
}

static int connect_tls(struct HTTPNetworkContext *ctx, const char *host, uint16_t port, const char *cacert) {
    bool has_cacert = (cacert && cacert[0] != '\0');
    if (!has_cacert) {
        log_warn("No CA certificate provided - server verification disabled");
    }

    tls_config_t cfg = {
        .host         = host,
        .port         = port,
        .sni          = host,
        .cacert       = has_cacert ? cacert : NULL,
        .verify       = TLS_VERIFY_NONE,   // no CA -> no verification (legacy behaviour)
        .force_tls12  = true,
        .ciphersuites = tls_ciphersuites_tuya_default(),
        .connect_timeout_ms = ctx->timeout_ms,
        .pal          = ctx->pal,
    };

    ctx->tls = tls_connect(&cfg);
    if (!ctx->tls) {
        log_error("Failed to establish TLS connection to %s:%d", host, port);
        return OPRT_TLS_HANDSHAKE_FAILED;
    }
    ctx->use_tls = true;
    log_debug("TLS connection established to %s:%d", host, port);
    return OPRT_OK;
}

// Close connection
static void disconnect(struct HTTPNetworkContext *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->use_tls) {
        if (ctx->tls) {
            tls_close(ctx->tls);   // sends close_notify, closes the socket, frees state
            ctx->tls = NULL;
        }
        ctx->use_tls = false;
    } else {
        if (ctx->tcp_handle) {
            ctx->pal->tcp_close(ctx->tcp_handle);
            ctx->tcp_handle = NULL;
        }
    }
}

http_client_status_t http_client_request(const http_client_request_t *request,
                                        http_client_response_t *response)
{
    if (!request || !response) {
        return HTTP_CLIENT_INVALID_PARAM;
    }

    if (!request->host || !request->method || !request->path) {
        return HTTP_CLIENT_INVALID_PARAM;
    }

    // Initialize response
    memset(response, 0, sizeof(http_client_response_t));

    const pal_t *pal = request->pal;

    // Determine if TLS should be used
    bool use_tls = (request->port == 443) ||
                   (request->cacert != NULL && request->cacert[0] != '\0');
    if (request->port == 80) {
        use_tls = false;
    }

    // Allocate network context
    struct HTTPNetworkContext *network_ctx = (struct HTTPNetworkContext *)pal->malloc(sizeof(struct HTTPNetworkContext));
    if (!network_ctx) {
        log_error("Failed to allocate network context");
        return HTTP_CLIENT_ERROR;
    }

    network_ctx->tcp_handle = NULL;
    network_ctx->use_tls = false;
    network_ctx->tls = NULL;
    network_ctx->host = request->host;
    network_ctx->port = request->port;
    network_ctx->pal = pal;
    network_ctx->timeout_ms = request->timeout_ms > 0 ? request->timeout_ms : IOT_HTTP_TIMEOUT_MS_DEFAULT;

    // Connect to server
    int connect_ret = OPRT_COMMUNICATION_ERROR;
    if (use_tls) {
        connect_ret = connect_tls(network_ctx, request->host, request->port,
                                  request->cacert);
    } else {
        network_ctx->tcp_handle = connect_tcp(network_ctx->pal, request->host, request->port,
                                              network_ctx->timeout_ms);
        connect_ret = network_ctx->tcp_handle ? 0 : OPRT_COMMUNICATION_ERROR;
    }

    if (connect_ret != 0) {
        log_error("Failed to connect to %s:%d", request->host, request->port);
        pal->free(network_ctx);
        return (connect_ret == OPRT_TLS_HANDSHAKE_FAILED) ? HTTP_CLIENT_TLS_ERROR : HTTP_CLIENT_ERROR;
    }

    // Setup transport interface
    TransportInterface_t transport = {
        .recv = transport_recv,
        .send = transport_send,
        .writev = NULL,
        .pNetworkContext = (NetworkContext_t *)network_ctx
    };

    // Prepare request headers buffer (allocate on heap to avoid stack overflow)
    #define REQUEST_HEADER_BUFFER_SIZE 1024
    uint8_t *request_header_buffer = (uint8_t *)pal->malloc(REQUEST_HEADER_BUFFER_SIZE);
    if (!request_header_buffer) {
        log_error("Failed to allocate request header buffer");
        disconnect(network_ctx);
        pal->free(network_ctx);
        return HTTP_CLIENT_ERROR;
    }
    HTTPRequestHeaders_t request_headers = {
        .pBuffer = request_header_buffer,
        .bufferLen = REQUEST_HEADER_BUFFER_SIZE,
        .headersLen = 0
    };

    // Prepare request info
    HTTPRequestInfo_t request_info = {
        .pMethod = request->method,
        .methodLen = strlen(request->method),
        .pPath = request->path,
        .pathLen = strlen(request->path),
        .pHost = request->host,
        .hostLen = strlen(request->host),
        .reqFlags = 0
    };

    // Initialize request headers
    HTTPStatus_t http_status = HTTPClient_InitializeRequestHeaders(&request_headers, &request_info);
    if (http_status != HTTPSuccess) {
        log_error("Failed to initialize request headers: %d", http_status);
        pal->free(request_header_buffer);
        disconnect(network_ctx);
        pal->free(network_ctx);
        return HTTP_CLIENT_ERROR;
    }

    // Add custom headers
    for (uint8_t i = 0; i < request->headers_count; i++) {
        http_status = HTTPClient_AddHeader(&request_headers,
                                          request->headers[i].key,
                                          strlen(request->headers[i].key),
                                          request->headers[i].value,
                                          strlen(request->headers[i].value));
        if (http_status != HTTPSuccess) {
            log_error("Failed to add header %s: %d", request->headers[i].key, http_status);
            pal->free(request_header_buffer);
            disconnect(network_ctx);
            pal->free(network_ctx);
            return HTTP_CLIENT_ERROR;
        }
    }

    // Prepare response buffer (allocate on heap to avoid stack overflow)
    #define RESPONSE_BUFFER_SIZE 4096
    uint8_t *response_buffer = (uint8_t *)pal->malloc(RESPONSE_BUFFER_SIZE);
    if (!response_buffer) {
        log_error("Failed to allocate response buffer");
        pal->free(request_header_buffer);
        disconnect(network_ctx);
        pal->free(network_ctx);
        return HTTP_CLIENT_ERROR;
    }
    HTTPResponse_t http_response = {
        .pBuffer = response_buffer,
        .bufferLen = RESPONSE_BUFFER_SIZE,
        .pHeaderParsingCallback = NULL,
        .getTime = NULL,
        .pHeaders = NULL,
        .headersLen = 0,
        .pBody = NULL,
        .bodyLen = 0,
        .statusCode = 0,
        .contentLength = 0,
        .headerCount = 0,
        .areHeadersComplete = 0,
        .respOptionFlags = 0,
        .respFlags = 0
    };

    // Send HTTP request
    http_status = HTTPClient_Send(&transport,
                                  &request_headers,
                                  request->body,
                                  request->body_length,
                                  &http_response,
                                  0);  // No special flags

    // Free request header buffer after HTTPClient_Send (no longer needed)

    http_client_status_t ret_status = HTTP_CLIENT_SUCCESS;

    if (http_status == HTTPSuccess) {
        // Extract status code
        response->status_code = http_response.statusCode;

        // Copy response body BEFORE freeing response_buffer
        // http_response.pBody may point into response_buffer
        // Allocate +1 byte for null terminator (body is often used as string)
        if (http_response.bodyLen > 0 && http_response.pBody) {
            response->body = (uint8_t *)pal->malloc(http_response.bodyLen + 1);
            if (response->body) {
                memcpy(response->body, http_response.pBody, http_response.bodyLen);
                response->body[http_response.bodyLen] = '\0';
                response->body_length = http_response.bodyLen;
            } else {
                log_error("Failed to allocate response body buffer");
                ret_status = HTTP_CLIENT_ERROR;
                disconnect(network_ctx);
                pal->free(network_ctx);
                response->internal = NULL;
                pal->free(request_header_buffer);
                pal->free(response_buffer);
                return ret_status;
            }
        }

        response->internal = NULL;

        log_debug("HTTP request successful: status=%d, body_len=%d",
                 response->status_code, (int)response->body_length);
    } else {
        log_error("HTTP request failed: %d", http_status);

        // Map coreHTTP status to our status
        switch (http_status) {
            case HTTPNetworkError:
                ret_status = HTTP_CLIENT_ERROR;
                break;
            case HTTPInvalidParameter:
                ret_status = HTTP_CLIENT_INVALID_PARAM;
                break;
            case HTTPInsufficientMemory:
                ret_status = HTTP_CLIENT_ERROR;
                break;
            default:
                ret_status = HTTP_CLIENT_ERROR;
                break;
        }

    }
    pal->free(request_header_buffer);
    pal->free(response_buffer);
    disconnect(network_ctx);
    pal->free(network_ctx);
    return ret_status;
}

void http_client_free(const pal_t *pal, http_client_response_t *response)
{
    if (!response) {
        return;
    }

    if (response->body) {
        pal->free(response->body);
        response->body = NULL;
    }

    response->body_length = 0;
    response->status_code = 0;
}
