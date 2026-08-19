#ifndef __IOT_CLIENT_MESSAGE_H__
#define __IOT_CLIENT_MESSAGE_H__

#include "iot_client.h"

/**
 * @brief Connect to the MQTT broker and subscribe to the device's inbound topic.
 *
 * On TLS handshake failure, automatically refreshes the MQTT CA certificate
 * and retries once.
 *
 * @param client  IoT client (must have mqtt_url and devid set)
 * @return OPRT_OK on success, OPRT_INVALID_PARAMETER if client/url/devid is missing
 */
int iot_client_message_connect(iot_client_t *client);

/**
 * @brief Disconnect from the MQTT broker and destroy the MQTT client.
 *
 * Safe to call with NULL or when not connected (no-op).
 *
 * @param client  IoT client instance
 */
void iot_client_message_disconnect(iot_client_t *client);

/**
 * @brief Process incoming MQTT messages.
 *
 * Receives and decrypts messages, invoking the client's message_callback
 * for each. Call this in a loop.
 *
 * @param client     IoT client instance
 * @param timeout_ms Maximum time to wait for messages in milliseconds
 * @return OPRT_OK on success, OPRT_UNINITIALIZED if client or MQTT handle is NULL
 */
int iot_client_message_process(iot_client_t *client, uint32_t timeout_ms);

/**
 * @brief Encrypt and publish a message to the device's outbound MQTT topic.
 *
 * Encrypts @p data with AES-128-GCM (P2.3) using the client's local_key,
 * then publishes to smart/device/out/{devid}.
 *
 * @param client   IoT client instance
 * @param data     Plaintext payload to encrypt and publish
 * @param data_len Length of @p data in bytes
 * @return OPRT_OK on success, OPRT_UNINITIALIZED if MQTT not connected,
 *         OPRT_INVALID_PARAMETER if data is NULL or empty
 */
int iot_client_message_publish(iot_client_t *client,
                               const uint8_t *data, size_t data_len);

/**
 * @brief Check if a decrypted MQTT payload is a cloud device-remove notice
 *        (protocol 11) and fire the reset callback if so.
 *
 * Parses the plaintext JSON, classifies the reset type (remote unbind vs.
 * factory reset) by the root-level "type" field, and fires the client's
 * reset_callback. Returns true (consumed) for protocol-11 envelopes so they
 * never reach the DP layer or the raw message callback.
 *
 * @param client  IoT client (must have devid and reset_callback set)
 * @param bytes   Decrypted payload bytes
 * @param len     Length of payload
 * @return true if the message was a protocol-11 reset notice (consumed),
 *         false for any other payload (passthrough)
 */
bool iot_client_message_handle_reset(iot_client_t *client,
                                     const uint8_t *bytes, size_t len);

#endif /* __IOT_CLIENT_MESSAGE_H__ */
