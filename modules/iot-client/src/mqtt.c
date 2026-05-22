#include "mqtt.h"
#include "iot_config_defaults.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "core_mqtt.h"

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 4096
#endif

#ifndef MQTT_SEND_TIMEOUT_MS
#define MQTT_SEND_TIMEOUT_MS 2000U
#endif

#ifndef MQTT_RECV_TIMEOUT_MS
#define MQTT_RECV_TIMEOUT_MS 1000U
#endif

// mbedtls headers for TLS support
#include "mbedtls/version.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"

// mbedtls runtime state — allocated on demand for TLS connections only.
// In TCP-only mode (mqtt_disable_tls=true) this stays NULL, saving ~3.3 KB.
typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config ssl_conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca_cert;
} mqtt_tls_state_t;

// Network context structure definition with TLS support
struct NetworkContext {
    void *tcp_handle;
    bool use_tls;
    mqtt_tls_state_t *tls;       // NULL when use_tls is false
    struct mqtt_client *client;  // Pointer to parent mqtt_client for callback access
};

#define MAX_BROKER_HOST_LEN 64
#define MAX_CLIENT_ID_LEN   32
#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    20
#define MAX_TOPIC_LEN       64
#define X509_VERIFY_BUF_LEN 512
#define MQTT_QOS_RECORD_COUNT 4

// MQTT client structure
struct mqtt_client {
    MQTTContext_t mqtt_context;
    NetworkContext_t network_context;
    TransportInterface_t transport;
    MQTTFixedBuffer_t fixed_buffer;
    uint8_t *buffer;

    // QoS1 and QoS2 publish records
    MQTTPubAckInfo_t incoming_publish_records[MQTT_QOS_RECORD_COUNT];
    MQTTPubAckInfo_t outgoing_publish_records[MQTT_QOS_RECORD_COUNT];

    char broker_host[MAX_BROKER_HOST_LEN];
    int broker_port;
    char client_id[MAX_CLIENT_ID_LEN];
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char subscribe_topic[MAX_TOPIC_LEN];
    mqtt_message_callback_t message_callback;
    void *user_data;
    bool connected;
    bool use_tls;
    const char *cacert;
    const pal_t *pal;

    // SUBACK tracking
    volatile int suback_status;
};

static uint64_t (*s_timestamp_ms_func)(void) = NULL;

static uint32_t mqtt_get_time_ms(void) {
    if (s_timestamp_ms_func) {
        return (uint32_t)s_timestamp_ms_func();
    }
    return 0;
}

static int32_t transport_send(NetworkContext_t *pNetworkContext,
                              const void *pBuffer,
                              size_t bytesToSend) {
    if (!pNetworkContext) {
        return OPRT_UNINITIALIZED;
    }

    if (pNetworkContext->use_tls) {
        int ret = mbedtls_ssl_write(&pNetworkContext->tls->ssl, pBuffer, bytesToSend);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
            return 0;
        }
        if (ret < 0) {
            char err_buf[64];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            log_error("TLS write error: %s (-%#x)", err_buf, -ret);
            return OPRT_COMMUNICATION_ERROR;
        }
        return ret;
    } else {
        if (!pNetworkContext->tcp_handle) {
            return OPRT_COMMUNICATION_ERROR;
        }
        int bytes_sent = pNetworkContext->client->pal->tcp_send(
            pNetworkContext->tcp_handle, (const uint8_t *)pBuffer, bytesToSend,
            MQTT_SEND_TIMEOUT_MS);
        if (bytes_sent == PAL_ERR_AGAIN) {
            return 0;
        }
        if (bytes_sent < 0) {
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
        return OPRT_UNINITIALIZED;
    }

    if (pNetworkContext->use_tls) {
        int ret = mbedtls_ssl_read(&pNetworkContext->tls->ssl, pBuffer, bytesToRecv);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return OPRT_OK;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            return OPRT_OK;
        }
        if (ret < 0) {
            char err_buf[64];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            log_error("TLS read error: %s (-%#x)", err_buf, -ret);
            return OPRT_COMMUNICATION_ERROR;
        }
        return ret;
    } else {
        if (!pNetworkContext->tcp_handle) {
            return OPRT_COMMUNICATION_ERROR;
        }
        int bytes_received = pNetworkContext->client->pal->tcp_recv(
            pNetworkContext->tcp_handle, (uint8_t *)pBuffer, bytesToRecv,
            MQTT_RECV_TIMEOUT_MS);
        if (bytes_received == PAL_ERR_AGAIN) {
            return OPRT_OK;
        }
        if (bytes_received < 0) {
            return OPRT_COMMUNICATION_ERROR;
        }
        return (int32_t)bytes_received;
    }
}

// Parse broker URL (tcp://host:port)
static int parse_broker_url(const char *url, char *host, int *port) {
    const char *host_start = strstr(url, "://");
    if (!host_start) {
        return OPRT_INVALID_PARAMETER;
    }
    host_start += 3;

    const char *port_start = strchr(host_start, ':');
    if (!port_start) {
        return OPRT_INVALID_PARAMETER;
    }

    size_t host_len = port_start - host_start;
    if (host_len >= MAX_BROKER_HOST_LEN) {
        return OPRT_INVALID_PARAMETER;
    }

    strncpy(host, host_start, host_len);
    host[host_len] = '\0';

    *port = atoi(port_start + 1);
    return OPRT_OK;
}

// Establish TCP connection to broker (non-TLS)
static void *connect_to_broker(mqtt_client *client) {
    void *handle = client->pal->tcp_connect(client->broker_host, (uint16_t)client->broker_port);
    if (!handle) {
        log_error("Failed to connect to broker %s:%d", client->broker_host, client->broker_port);
        return NULL;
    }
    log_info("TCP connection established to %s:%d", client->broker_host, client->broker_port);
    return handle;
}

static int tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    NetworkContext_t *network_ctx = (NetworkContext_t *)ctx;
    int ret = network_ctx->client->pal->tcp_send(network_ctx->tcp_handle, buf, len, 0);
    if (ret == PAL_ERR_AGAIN) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    if (ret < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int tls_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout)
{
    NetworkContext_t *network_ctx = (NetworkContext_t *)ctx;
    int ret = network_ctx->client->pal->tcp_recv(network_ctx->tcp_handle, buf, len, timeout);
    if (ret == PAL_ERR_AGAIN) {
        return timeout == 0 ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_SSL_TIMEOUT;
    }
    if (ret < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

// Returns: 0 on success, OPRT_TLS_HANDSHAKE_FAILED on TLS error, OPRT_COMMUNICATION_ERROR on other errors
static int connect_to_broker_tls(NetworkContext_t *network_ctx, const char *host, int port, const char *cacert) {
    int ret;
    int result = OPRT_COMMUNICATION_ERROR;

    bool has_cacert = (cacert && cacert[0] != '\0');
    if (!has_cacert) {
        log_warn("No CA certificate provided - server verification disabled");
    }

    network_ctx->tls = network_ctx->client->pal->malloc(sizeof(mqtt_tls_state_t));
    if (!network_ctx->tls) {
        log_error("Failed to allocate TLS state (%zu bytes)", sizeof(mqtt_tls_state_t));
        return OPRT_MALLOC_FAILED;
    }
    mqtt_tls_state_t *tls = network_ctx->tls;

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->ssl_conf);
    mbedtls_x509_crt_init(&tls->ca_cert);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
    mbedtls_entropy_init(&tls->entropy);

    const char *pers = "mqtt_client";
    ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func, &tls->entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        log_error("Failed to seed RNG: -%#x", -ret);
        result = OPRT_TLS_HANDSHAKE_FAILED;
        goto cleanup;
    }

    if (has_cacert) {
        const char *pem_header = "-----BEGIN CERTIFICATE-----";
        if (strstr(cacert, pem_header) != NULL) {
            ret = mbedtls_x509_crt_parse(&tls->ca_cert,
                                         (const unsigned char *)cacert,
                                         strlen(cacert) + 1);
        } else {
            size_t pem_len = strlen(cacert) + 64 + 2;
            char *pem_buf = network_ctx->client->pal->malloc(pem_len);
            if (pem_buf) {
                memset(pem_buf, 0, pem_len);
                snprintf(pem_buf, pem_len,
                         "-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n",
                         cacert);
                ret = mbedtls_x509_crt_parse(&tls->ca_cert,
                                             (const unsigned char *)pem_buf,
                                             strlen(pem_buf) + 1);
                network_ctx->client->pal->free(pem_buf);
            } else {
                ret = OPRT_MALLOC_FAILED;
            }
        }
        if (ret != 0) {
            log_error("Failed to parse CA certificate: -%#x", -ret);
            result = OPRT_TLS_HANDSHAKE_FAILED;
            goto cleanup;
        }
        log_info("Loaded CA certificate");
    }

    network_ctx->tcp_handle = network_ctx->client->pal->tcp_connect(host, (uint16_t)port);
    if (!network_ctx->tcp_handle) {
        log_error("Failed to connect to %s:%d", host, port);
        goto cleanup;
    }
    log_info("TCP connection established to %s:%d", host, port);

    ret = mbedtls_ssl_config_defaults(&tls->ssl_conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        log_error("Failed to set SSL config defaults: -%#x", -ret);
        result = OPRT_TLS_HANDSHAKE_FAILED;
        goto cleanup;
    }

#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_ssl_conf_min_tls_version(&tls->ssl_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&tls->ssl_conf, MBEDTLS_SSL_VERSION_TLS1_2);
#else
    mbedtls_ssl_conf_min_version(&tls->ssl_conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&tls->ssl_conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
#endif

    static const int ciphersuites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        0
    };
    mbedtls_ssl_conf_ciphersuites(&tls->ssl_conf, ciphersuites);
    log_info("Configured TLS 1.2 with ECDHE-{ECDSA,RSA}-AES128-GCM-SHA256");

    if (has_cacert) {
        mbedtls_ssl_conf_authmode(&tls->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&tls->ssl_conf, &tls->ca_cert, NULL);
    } else {
        mbedtls_ssl_conf_authmode(&tls->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    mbedtls_ssl_conf_rng(&tls->ssl_conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);

    ret = mbedtls_ssl_setup(&tls->ssl, &tls->ssl_conf);
    if (ret != 0) {
        log_error("Failed to setup SSL: -%#x", -ret);
        result = OPRT_TLS_HANDSHAKE_FAILED;
        goto cleanup;
    }

    ret = mbedtls_ssl_set_hostname(&tls->ssl, host);
    if (ret != 0) {
        log_error("Failed to set hostname: -%#x", -ret);
        result = OPRT_TLS_HANDSHAKE_FAILED;
        goto cleanup;
    }

    mbedtls_ssl_conf_read_timeout(&tls->ssl_conf, MQTT_RECV_TIMEOUT_MS);
    mbedtls_ssl_set_bio(&tls->ssl, network_ctx,
                       tls_send, NULL, tls_recv_timeout);

    log_info("Starting TLS handshake...");
    while ((ret = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char err_buf[64];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            log_error("TLS handshake failed: %s (-%#x)", err_buf, -ret);
            result = OPRT_TLS_HANDSHAKE_FAILED;
            goto cleanup;
        }
    }

    const char *version = mbedtls_ssl_get_version(&tls->ssl);
    const char *cipher = mbedtls_ssl_get_ciphersuite(&tls->ssl);
    log_info("TLS handshake successful - Version: %s, Cipher: %s", version, cipher);

    if (has_cacert) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
        if (flags != 0) {
            char *vrfy_buf = (char *)network_ctx->client->pal->malloc(X509_VERIFY_BUF_LEN);
            if (vrfy_buf) {
                mbedtls_x509_crt_verify_info(vrfy_buf, X509_VERIFY_BUF_LEN, "  ! ", flags);
                log_error("Certificate verification failed:\n%s", vrfy_buf);
                network_ctx->client->pal->free(vrfy_buf);
            }
            result = OPRT_TLS_HANDSHAKE_FAILED;
            goto cleanup;
        }
        log_info("Certificate verification passed");
    }

    log_info("TLS connection established to %s:%d", host, port);
    network_ctx->use_tls = true;
    return OPRT_OK;

cleanup:
    if (network_ctx->tcp_handle) {
        network_ctx->client->pal->tcp_close(network_ctx->tcp_handle);
        network_ctx->tcp_handle = NULL;
    }
    mbedtls_x509_crt_free(&tls->ca_cert);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->ssl_conf);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
    network_ctx->client->pal->free(network_ctx->tls);
    network_ctx->tls = NULL;
    return result;
}

static void tls_cleanup(NetworkContext_t *ctx)
{
    if (!ctx->tls) {
        return;
    }
    if (ctx->tcp_handle) {
        ctx->client->pal->tcp_close(ctx->tcp_handle);
        ctx->tcp_handle = NULL;
    }
    mbedtls_x509_crt_free(&ctx->tls->ca_cert);
    mbedtls_ssl_free(&ctx->tls->ssl);
    mbedtls_ssl_config_free(&ctx->tls->ssl_conf);
    mbedtls_ctr_drbg_free(&ctx->tls->ctr_drbg);
    mbedtls_entropy_free(&ctx->tls->entropy);
    ctx->client->pal->free(ctx->tls);
    ctx->tls = NULL;
}

static void mqtt_event_callback(MQTTContext_t *pMqttContext,
                                MQTTPacketInfo_t *pPacketInfo,
                                MQTTDeserializedInfo_t *pDeserializedInfo) {
    NetworkContext_t *pNetworkContext = (NetworkContext_t *)pMqttContext->transportInterface.pNetworkContext;
    if (!pNetworkContext || !pNetworkContext->client) {
        log_warn("mqtt_event_callback: invalid network context");
        return;
    }

    mqtt_client *client = pNetworkContext->client;

    log_debug("MQTT event callback: packet type = 0x%02X", pPacketInfo->type);

    if ((pPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        MQTTPublishInfo_t *pPublish = pDeserializedInfo->pPublishInfo;
        log_info("Received PUBLISH: topic=%.*s, payload_len=%u",
                 (int)pPublish->topicNameLength, pPublish->pTopicName, (unsigned)pPublish->payloadLength);
        if (client->message_callback) {
            client->message_callback(pPublish->pTopicName, pPublish->topicNameLength,
                                   pPublish->pPayload, pPublish->payloadLength,
                                   client->user_data);
        }
    } else if (pPacketInfo->type == MQTT_PACKET_TYPE_SUBACK) {
        // Parse SUBACK: remaining data contains packet_id (2 bytes) + return codes
        // pPacketInfo->pRemainingData points to variable header + payload
        if (pPacketInfo->remainingLength >= 3) {
            // Skip packet ID (2 bytes), get first return code
            uint8_t return_code = pPacketInfo->pRemainingData[2];
            if (return_code == 0x80) {
                log_error("Received SUBACK with failure code 0x%02X", return_code);
                client->suback_status = return_code;
            } else {
                log_info("Received SUBACK with granted QoS %d", return_code);
                client->suback_status = 0;
            }
        } else {
            log_warn("Received SUBACK with invalid length");
            client->suback_status = 0x80;
        }
    } else if (pPacketInfo->type == MQTT_PACKET_TYPE_PUBACK) {
        log_debug("Received PUBACK");
    }
}

// Create MQTT client with full configuration
mqtt_client *mqtt_client_create_with_config(const mqtt_client_config_t *config) {
    if (!config) {
        log_error("Invalid parameters for MQTT client creation");
        return NULL;
    }
    if (!config->pal) {
        log_error("mqtt_client_create_with_config: config->pal is NULL");
        return NULL;
    }
    if (!config->broker_url || !config->client_id ||
        !config->password || !config->subscribe_topic) {
        log_error("Invalid parameters for MQTT client creation");
        return NULL;
    }

    const pal_t *pal = config->pal;
    mqtt_client *client = (mqtt_client *)pal->malloc(sizeof(mqtt_client));
    if (!client) {
        log_error("Failed to allocate memory for MQTT client");
        return NULL;
    }
    memset(client, 0, sizeof(mqtt_client));
    client->pal = pal;

    // Parse broker URL
    if (parse_broker_url(config->broker_url, client->broker_host, &client->broker_port) != 0) {
        log_error("Failed to parse broker URL: %s", config->broker_url);
        client->pal->free(client);
        return NULL;
    }

    // Detect if TLS should be used
    client->use_tls = (strncmp(config->broker_url, "mqtts://", 8) == 0 ||
                       strncmp(config->broker_url, "ssl://", 6) == 0);

    if (client->use_tls) {
        if (config->tls_config && config->tls_config->cacert && config->tls_config->cacert[0] != '\0') {
            client->cacert = config->tls_config->cacert;
            log_info("TLS enabled with CA cert PEM");
        } else {
            client->cacert = NULL;
            log_warn("TLS enabled without CA certificate - server verification disabled");
        }
    }

    // Copy client configuration
    strncpy(client->client_id, config->client_id, sizeof(client->client_id) - 1);
    client->client_id[sizeof(client->client_id) - 1] = '\0';
    // Use separate username if provided, otherwise use client_id
    if (config->username && strlen(config->username) > 0) {
        strncpy(client->username, config->username, sizeof(client->username) - 1);
    } else {
        strncpy(client->username, config->client_id, sizeof(client->username) - 1);
    }
    client->username[sizeof(client->username) - 1] = '\0';
    strncpy(client->password, config->password, sizeof(client->password) - 1);
    client->password[sizeof(client->password) - 1] = '\0';
    strncpy(client->subscribe_topic, config->subscribe_topic, sizeof(client->subscribe_topic) - 1);
    client->subscribe_topic[sizeof(client->subscribe_topic) - 1] = '\0';
    client->message_callback = config->callback;
    client->user_data = config->user_data;
    client->connected = false;

    // Buffer is lazy-allocated on connect to avoid 4 KB inline footprint
    client->buffer = NULL;
    client->fixed_buffer.pBuffer = NULL;
    client->fixed_buffer.size = 0;

    // Setup transport interface
    client->transport.send = transport_send;
    client->transport.recv = transport_recv;
    client->transport.pNetworkContext = &client->network_context;
    log_info("MQTT client created for broker %s:%d (TLS: %s), clientId: %s, username: %s",
             client->broker_host, client->broker_port, client->use_tls ? "yes" : "no",
             client->client_id, client->username);
    return client;
}

// Release the lazy MQTT fixed buffer; safe to call when buffer is already NULL.
static void release_mqtt_buffer(mqtt_client *client) {
    if (!client->buffer) {
        return;
    }
    client->pal->free(client->buffer);
    client->buffer = NULL;
    client->fixed_buffer.pBuffer = NULL;
    client->fixed_buffer.size = 0;
}

// Connect to MQTT broker
int mqtt_client_connect(mqtt_client *client) {
    if (!client) {
        return OPRT_INVALID_PARAMETER;
    }

    s_timestamp_ms_func = client->pal->time_ms;

    // Allocate the MQTT fixed buffer on demand (freed on disconnect/destroy)
    if (!client->buffer) {
        client->buffer = client->pal->malloc(MQTT_MAX_PACKET_SIZE);
        if (!client->buffer) {
            log_error("Failed to allocate MQTT buffer (%u bytes)", MQTT_MAX_PACKET_SIZE);
            return OPRT_MALLOC_FAILED;
        }
        client->fixed_buffer.pBuffer = client->buffer;
        client->fixed_buffer.size = MQTT_MAX_PACKET_SIZE;
    }

    // Set client pointer in network context for callback access
    client->network_context.client = client;

    // Establish connection (TCP or TLS)
    if (client->use_tls) {
        int tls_ret = connect_to_broker_tls(&client->network_context, client->broker_host,
                                            client->broker_port, client->cacert);
        if (tls_ret != 0) {
            release_mqtt_buffer(client);
            return tls_ret;
        }
    } else {
        client->network_context.tcp_handle = connect_to_broker(client);
        if (!client->network_context.tcp_handle) {
            release_mqtt_buffer(client);
            return OPRT_COMMUNICATION_ERROR;
        }
        client->network_context.use_tls = false;
    }

    // Initialize MQTT context
    MQTTStatus_t status = MQTT_Init(&client->mqtt_context, &client->transport,
                                    mqtt_get_time_ms, mqtt_event_callback, &client->fixed_buffer);
    if (status != MQTTSuccess) {
        log_error("MQTT_Init failed: %d", status);
        if (client->use_tls) {
            tls_cleanup(&client->network_context);
        } else {
            client->pal->tcp_close(client->network_context.tcp_handle);
            client->network_context.tcp_handle = NULL;
        }
        release_mqtt_buffer(client);
        return OPRT_COMMUNICATION_ERROR;
    }

    // Initialize QoS1 and QoS2 support
    status = MQTT_InitStatefulQoS(&client->mqtt_context,
                                   client->outgoing_publish_records,
                                   MQTT_QOS_RECORD_COUNT,
                                   client->incoming_publish_records,
                                   MQTT_QOS_RECORD_COUNT);
    if (status != MQTTSuccess) {
        log_error("MQTT_InitStatefulQoS failed: %d", status);
        if (client->use_tls) {
            tls_cleanup(&client->network_context);
        } else {
            client->pal->tcp_close(client->network_context.tcp_handle);
            client->network_context.tcp_handle = NULL;
        }
        release_mqtt_buffer(client);
        return OPRT_COMMUNICATION_ERROR;
    }
    log_info("QoS1 and QoS2 support initialized");

    // Setup CONNECT packet
    MQTTConnectInfo_t connect_info = {0};
    connect_info.cleanSession = true;
    connect_info.keepAliveSeconds = 60;
    connect_info.pClientIdentifier = client->client_id;
    connect_info.clientIdentifierLength = strlen(client->client_id);
    connect_info.pUserName = client->username;
    connect_info.userNameLength = strlen(client->username);
    connect_info.pPassword = client->password;
    connect_info.passwordLength = strlen(client->password);

    log_info("MQTT CONNECT packet:");
    log_info("  clientId : [%u] %s", (unsigned)connect_info.clientIdentifierLength, connect_info.pClientIdentifier);
    log_info("  username : [%u] %s", (unsigned)connect_info.userNameLength, connect_info.pUserName);
    log_info("  password : [%u] ****", (unsigned)connect_info.passwordLength);
    log_info("  keepAlive: %u s, cleanSession: %d", connect_info.keepAliveSeconds, connect_info.cleanSession);

    bool sessionPresent = false;
    status = MQTT_Connect(&client->mqtt_context, &connect_info, NULL,
                         MQTT_SEND_TIMEOUT_MS, &sessionPresent);

    if (status != MQTTSuccess) {
        log_error("MQTT_Connect failed: %d", status);
        if (client->use_tls) {
            tls_cleanup(&client->network_context);
        } else {
            client->pal->tcp_close(client->network_context.tcp_handle);
            client->network_context.tcp_handle = NULL;
        }
        release_mqtt_buffer(client);
        return OPRT_COMMUNICATION_ERROR;
    }

    client->connected = true;
    log_info("Successfully connected to MQTT broker");
    return OPRT_OK;
}

// Subscribe to topic
int mqtt_client_subscribe(mqtt_client *client) {
    if (!client) {
        return OPRT_INVALID_PARAMETER;
    }
    if (!client->connected) {
        return OPRT_UNINITIALIZED;
    }

    MQTTSubscribeInfo_t subscribe_info = {0};
    subscribe_info.qos = MQTTQoS1;
    subscribe_info.pTopicFilter = client->subscribe_topic;
    subscribe_info.topicFilterLength = strlen(client->subscribe_topic);

    // Reset SUBACK status before subscribe
    client->suback_status = -1;

    uint16_t packet_id = MQTT_GetPacketId(&client->mqtt_context);
    MQTTStatus_t status = MQTT_Subscribe(&client->mqtt_context, &subscribe_info, 1, packet_id);

    if (status != MQTTSuccess) {
        log_error("MQTT_Subscribe failed: %d", status);
        return OPRT_COMMUNICATION_ERROR;
    }

    log_info("Subscribe request sent for topic: %s, waiting for SUBACK...", client->subscribe_topic);

    int retries = 50;
    while (retries-- > 0) {
        status = MQTT_ProcessLoop(&client->mqtt_context);
        if (status == MQTTSuccess) {
            if (client->suback_status == 0) {
                log_info("Successfully subscribed to topic: %s", client->subscribe_topic);
                return OPRT_OK;
            }
            if (client->suback_status > 0) {
                log_error("Subscription rejected by broker: code 0x%02X", client->suback_status);
                return OPRT_COMMUNICATION_ERROR;
            }
        } else if (status != MQTTNeedMoreBytes) {
            log_error("MQTT_ProcessLoop failed while waiting for SUBACK: %d", status);
            return OPRT_COMMUNICATION_ERROR;
        }
        usleep(100000);
    }

    log_error("Timeout waiting for SUBACK");
    return OPRT_COMMUNICATION_ERROR;
}

// Publish message
int mqtt_client_publish(mqtt_client *client, const char *topic,
                        const uint8_t *message, size_t message_len) {
    if (!client) {
        return OPRT_INVALID_PARAMETER;
    }
    if (!client->connected) {
        return OPRT_UNINITIALIZED;
    }

    MQTTPublishInfo_t publish_info = {0};
    publish_info.qos = MQTTQoS1;
    publish_info.retain = false;
    publish_info.pTopicName = topic;
    publish_info.topicNameLength = strlen(topic);
    publish_info.pPayload = message;
    publish_info.payloadLength = message_len;

    MQTTStatus_t status = MQTT_Publish(&client->mqtt_context, &publish_info,
                                      MQTT_GetPacketId(&client->mqtt_context));

    if (status != MQTTSuccess) {
        log_error("MQTT_Publish failed: %d", status);
        return OPRT_COMMUNICATION_ERROR;
    }

    log_debug("Published message to topic: %s", topic);
    return OPRT_OK;
}

// Process MQTT events
int mqtt_client_process(mqtt_client *client, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!client) {
        return OPRT_INVALID_PARAMETER;
    }
    if (!client->connected) {
        return OPRT_UNINITIALIZED;
    }

    MQTTStatus_t status = MQTT_ProcessLoop(&client->mqtt_context);

    if (status != MQTTSuccess && status != MQTTNeedMoreBytes) {
        log_error("MQTT_ProcessLoop failed: %d", status);
        return OPRT_COMMUNICATION_ERROR;
    }

    return OPRT_OK;
}

// Disconnect
void mqtt_client_disconnect(mqtt_client *client) {
    if (!client || !client->connected) {
        return;
    }

    MQTT_Disconnect(&client->mqtt_context);

    if (client->use_tls) {
        tls_cleanup(&client->network_context);
    } else {
        if (client->network_context.tcp_handle) {
            client->pal->tcp_close(client->network_context.tcp_handle);
            client->network_context.tcp_handle = NULL;
        }
    }

    release_mqtt_buffer(client);

    client->connected = false;

    log_info("Disconnected from MQTT broker");
}

// Destroy client
void mqtt_client_destroy(mqtt_client *client) {
    if (!client) {
        return;
    }

    if (client->connected) {
        mqtt_client_disconnect(client);
    }

    const pal_t *pal = client->pal;
    pal->free(client);
}

// Check if connected
bool mqtt_client_is_connected(mqtt_client *client) {
    return client && client->connected;
}
