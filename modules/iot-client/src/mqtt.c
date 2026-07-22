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

#ifndef MQTT_CONNECT_TIMEOUT_MS
#define MQTT_CONNECT_TIMEOUT_MS 10000U
#endif

// Shared TLS transport (mbedTLS lives entirely inside common/tls).
#include "tls.h"

// Network context structure definition with TLS support
struct NetworkContext {
    void *tcp_handle;
    bool use_tls;
    tls_t *tls;                  // NULL when use_tls is false
    struct mqtt_client *client;  // Pointer to parent mqtt_client for callback access
};

#define MAX_BROKER_HOST_LEN 64
#define MAX_CLIENT_ID_LEN   32
#define MAX_USERNAME_LEN    32
#define MAX_PASSWORD_LEN    20
#define MAX_TOPIC_LEN       64
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
    tls_cert_bundle_attach_fn cert_bundle_attach;
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
        int r = tls_write(pNetworkContext->tls, (const uint8_t *)pBuffer,
                          bytesToSend, 30000 /* 30s */);
        if (r != TLS_OK) {
            log_error("TLS write error");
            return OPRT_COMMUNICATION_ERROR;
        }
        return (int32_t)bytesToSend;
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
            log_error("TCP send failed: %d", bytes_sent);
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
        int n = tls_read(pNetworkContext->tls, (uint8_t *)pBuffer, bytesToRecv,
                         MQTT_RECV_TIMEOUT_MS);
        if (n == TLS_ERR_AGAIN) {
            return OPRT_OK;   // no data within timeout: coreMQTT polls again
        }
        if (n <= 0) {
            // n<0: TLS error. n==0: graceful peer close_notify — tls_read maps
            // no-data to TLS_ERR_AGAIN, so 0 here always means the peer closed.
            // Must surface as an error: returning 0 would look like an idle poll
            // and the dead link would only be noticed at the keepalive timeout.
            log_error("TLS read error/closed (n=%d)", n);
            return OPRT_COMMUNICATION_ERROR;
        }
        return n;   // >0 bytes
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
            log_error("TCP recv failed: %d", bytes_received);
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
    void *handle = client->pal->tcp_connect(client->broker_host, (uint16_t)client->broker_port,
                                            MQTT_CONNECT_TIMEOUT_MS);
    if (!handle) {
        log_error("Failed to connect to broker %s:%d", client->broker_host, client->broker_port);
        return NULL;
    }
    log_info("TCP connection established to %s:%d", client->broker_host, client->broker_port);
    return handle;
}

// Returns: OPRT_OK on success, OPRT_TLS_HANDSHAKE_FAILED on failure.
static int connect_to_broker_tls(NetworkContext_t *network_ctx, const char *host, int port,
                                 const char *cacert, tls_cert_bundle_attach_fn cert_bundle_attach) {
    bool has_cacert = (cacert && cacert[0] != '\0');
    if (!has_cacert && !cert_bundle_attach) {
        log_warn("No CA certificate provided - server verification disabled");
    }

    tls_config_t cfg = {
        .host         = host,
        .port         = (uint16_t)port,
        .sni          = host,
        .cacert       = has_cacert ? cacert : NULL,
        .cert_bundle_attach = cert_bundle_attach,
        .verify       = TLS_VERIFY_NONE,   // no CA -> no verification (legacy behaviour)
        .force_tls12  = true,
        .ciphersuites = tls_ciphersuites_tuya_default(),
        .connect_timeout_ms = MQTT_CONNECT_TIMEOUT_MS,
        .pal          = network_ctx->client->pal,
    };

    network_ctx->tls = tls_connect(&cfg);
    if (!network_ctx->tls) {
        log_error("Failed to establish TLS connection to %s:%d", host, port);
        return OPRT_TLS_HANDSHAKE_FAILED;
    }
    network_ctx->use_tls = true;
    log_info("TLS connection established to %s:%d", host, port);
    return OPRT_OK;
}

static void tls_cleanup(NetworkContext_t *ctx)
{
    if (!ctx->tls) {
        return;
    }
    tls_close(ctx->tls);   // sends close_notify, closes the socket, frees state
    ctx->tls = NULL;
    ctx->tcp_handle = NULL;
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
            if (config->tls_config && config->tls_config->cert_bundle_attach) {
                log_info("TLS enabled with platform cert bundle");
            } else {
                log_warn("TLS enabled without CA certificate - server verification disabled");
            }
        }
        if (config->tls_config)
            client->cert_bundle_attach = config->tls_config->cert_bundle_attach;
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
                                            client->broker_port, client->cacert,
                                            client->cert_bundle_attach);
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
