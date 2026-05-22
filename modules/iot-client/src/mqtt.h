#ifndef __MQTT_H__
#define __MQTT_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iot_client.h"

// MQTT message callback type
typedef void (*mqtt_message_callback_t)(const char *topic, size_t topic_len,
                                        const uint8_t *payload, size_t payload_len,
                                        void *user_data);

// MQTT client structure
typedef struct mqtt_client mqtt_client;

/**
 * @brief TLS configuration for MQTT connection
 */
typedef struct {
    const char *cacert;            // CA certificate PEM content
    const char *client_cert;       // Client certificate PEM content (NULL if not needed)
    const char *client_key;        // Client key PEM content (NULL if not needed)
    bool verify_peer;              // Whether to verify peer certificate
} mqtt_tls_config_t;

/**
 * @brief MQTT client configuration
 */
typedef struct {
    const char *broker_url;        // MQTT broker URL (e.g., "tcp://host:1883" or "mqtts://host:8883")
    const char *client_id;         // Client ID
    const char *username;          // Username (can be different from client_id)
    const char *password;          // Password
    const char *subscribe_topic;   // Topic to subscribe to
    mqtt_message_callback_t callback;  // Message callback function
    void *user_data;                   // User data passed to callback
    const mqtt_tls_config_t *tls_config;  // TLS configuration (NULL for non-TLS)
    const pal_t *pal;              // PAL adapter
} mqtt_client_config_t;

/**
 * @brief Create a new MQTT client with full configuration
 *
 * @param config MQTT client configuration
 * @return mqtt_client* Pointer to MQTT client or NULL on failure
 */
mqtt_client *mqtt_client_create_with_config(const mqtt_client_config_t *config);

/**
 * @brief Connect to MQTT broker
 *
 * @param client MQTT client pointer
 * @return int 0 on success, -1 on failure
 */
int mqtt_client_connect(mqtt_client *client);

/**
 * @brief Subscribe to topic
 *
 * @param client MQTT client pointer
 * @return int 0 on success, -1 on failure
 */
int mqtt_client_subscribe(mqtt_client *client);

/**
 * @brief Publish message to topic
 *
 * @param client MQTT client pointer
 * @param topic Topic to publish to
 * @param message Message data
 * @param message_len Length of message
 * @return int 0 on success, -1 on failure
 */
int mqtt_client_publish(mqtt_client *client, const char *topic,
                        const uint8_t *message, size_t message_len);

/**
 * @brief Process MQTT events (call this in a loop)
 *
 * @param client MQTT client pointer
 * @param timeout_ms Timeout in milliseconds
 * @return int 0 on success, -1 on failure
 */
int mqtt_client_process(mqtt_client *client, uint32_t timeout_ms);

/**
 * @brief Disconnect from MQTT broker
 *
 * @param client MQTT client pointer
 */
void mqtt_client_disconnect(mqtt_client *client);

/**
 * @brief Destroy MQTT client and free resources
 *
 * @param client MQTT client pointer
 */
void mqtt_client_destroy(mqtt_client *client);

/**
 * @brief Check if client is connected
 *
 * @param client MQTT client pointer
 * @return bool true if connected, false otherwise
 */
bool mqtt_client_is_connected(mqtt_client *client);

#endif /* __MQTT_H__ */
