#include "iot_client_message.h"
#include "mqtt.h"
#include "cipher_wrapper.h"
#include "iot_config_defaults.h"
#include "iot_dp_internal.h"

#include <string.h>
#include <stdio.h>

static void mqtt_message_handler(const char *topic, size_t topic_len,
                                 const uint8_t *payload, size_t payload_len,
                                 void *user_data)
{
    iot_client_t *client = (iot_client_t *)user_data;
    if (!client) return;   /* DP dispatch may run even with no raw message_callback */

    uint8_t *decrypted = (uint8_t *)client->pal->malloc(payload_len);
    if (!decrypted) {
        log_error("Failed to allocate buffer for MQTT message decryption");
        return;
    }

    size_t decrypted_len = 0;
    int ret = pv23_decrypt(payload, payload_len,
                           (const uint8_t *)client->local_key,
                           decrypted, &decrypted_len);
    if (ret == 0 && decrypted_len > 0) {
        /* Offer the plaintext to the DP layer first; if it does not consume it,
         * forward to the user's raw message callback (backward compatible). */
        if (!iot_dp_dispatch_downlink(client, topic, topic_len, decrypted, decrypted_len)
            && client->message_callback) {
            client->message_callback(topic, topic_len, decrypted, decrypted_len);
        }
    } else {
        /* Decryption failed: never feed raw ciphertext to the DP parser. */
        log_warn("pv23_decrypt failed (ret=%d), forwarding raw payload", ret);
        if (client->message_callback) {
            client->message_callback(topic, topic_len, payload, payload_len);
        }
    }

    client->pal->free(decrypted);
}

static int iot_client_message_try_connect(iot_client_t *client)
{
    size_t devid_len = strlen(client->devid);
    size_t topic_buf_len = 17 + devid_len + 1;
    char *subscribe_topic = (char *)client->pal->malloc(topic_buf_len);
    if (!subscribe_topic)
        return OPRT_MALLOC_FAILED;
    int sn_ret = snprintf(subscribe_topic, topic_buf_len,
             "smart/device/in/%s", client->devid);
    if (sn_ret < 0 || (size_t)sn_ret >= topic_buf_len) {
        client->pal->free(subscribe_topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    char password[17] = {0};
    int md5_ret = iot_md5_password(client->secret_key, password);
    if (md5_ret != 0) {
        client->pal->free(subscribe_topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    mqtt_tls_config_t tls_cfg = { .cacert = client->cacert };
    mqtt_client_config_t mqtt_cfg = {
        .broker_url = client->mqtt_url,
        .client_id = client->devid,
        .username = client->devid,
        .password = password,
        .subscribe_topic = subscribe_topic,
        .callback = mqtt_message_handler,
        .user_data = client,
        .tls_config = &tls_cfg,
        .pal = client->pal,
    };

    client->mqtt = mqtt_client_create_with_config(&mqtt_cfg);
    if (!client->mqtt) {
        log_error("Failed to create MQTT client");
        client->pal->free(subscribe_topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    int ret = mqtt_client_connect(client->mqtt);
    if (ret != 0) {
        log_error("Failed to connect to MQTT broker: %d", ret);
        mqtt_client_destroy(client->mqtt);
        client->mqtt = NULL;
        client->pal->free(subscribe_topic);
        return (ret == OPRT_TLS_HANDSHAKE_FAILED) ? OPRT_TLS_HANDSHAKE_FAILED
                                                  : OPRT_COMMUNICATION_ERROR;
    }

    if (mqtt_client_subscribe(client->mqtt) != 0) {
        log_error("Failed to subscribe to %s", subscribe_topic);
        mqtt_client_destroy(client->mqtt);
        client->mqtt = NULL;
        client->pal->free(subscribe_topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    log_info("MQTT connected and subscribed to %s", subscribe_topic);
    client->pal->free(subscribe_topic);
    return OPRT_OK;
}

int iot_client_message_connect(iot_client_t *client)
{
    if (!client || !client->mqtt_url || client->devid[0] == '\0') {
        return OPRT_INVALID_PARAMETER;
    }

    return iot_client_message_try_connect(client);
}

void iot_client_message_disconnect(iot_client_t *client)
{
    if (client && client->mqtt) {
        mqtt_client_destroy(client->mqtt);
        client->mqtt = NULL;
    }
}

int iot_client_message_process(iot_client_t *client, uint32_t timeout_ms)
{
    if (!client || !client->mqtt) {
        return OPRT_UNINITIALIZED;
    }
    return mqtt_client_process(client->mqtt, timeout_ms);
}

#define PV23_OVERHEAD 40 /* AAD(12) + IV(12) + TAG(16) */

int iot_client_message_publish(iot_client_t *client,
                               const uint8_t *data, size_t data_len)
{
    if (!client || !client->mqtt) {
        return OPRT_UNINITIALIZED;
    }
    if (!data || data_len == 0) {
        return OPRT_INVALID_PARAMETER;
    }

    size_t enc_buf_len = data_len + PV23_OVERHEAD;
    uint8_t *encrypted = (uint8_t *)client->pal->malloc(enc_buf_len);
    if (!encrypted) {
        log_error("Failed to allocate buffer for message encryption");
        return OPRT_MALLOC_FAILED;
    }

    size_t encrypted_len = 0;
    int ret = pv23_encrypt(data, data_len,
                           (const uint8_t *)client->local_key,
                           encrypted, &encrypted_len);
    if (ret != 0) {
        log_error("pv23_encrypt failed: %d", ret);
        client->pal->free(encrypted);
        return OPRT_COMMUNICATION_ERROR;
    }

    size_t topic_len = 18 + strlen(client->devid) + 1;
    char *topic = (char *)client->pal->malloc(topic_len);
    if (!topic) {
        client->pal->free(encrypted);
        return OPRT_MALLOC_FAILED;
    }
    int sn = snprintf(topic, topic_len, "smart/device/out/%s", client->devid);
    if (sn < 0 || (size_t)sn >= topic_len) {
        client->pal->free(encrypted);
        client->pal->free(topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    ret = mqtt_client_publish(client->mqtt, topic, encrypted, encrypted_len);
    client->pal->free(encrypted);

    if (ret != 0) {
        log_error("Failed to publish to %s", topic);
        client->pal->free(topic);
        return OPRT_COMMUNICATION_ERROR;
    }

    log_debug("Published encrypted message to %s (%u bytes)", topic, (unsigned)encrypted_len);
    client->pal->free(topic);
    return OPRT_OK;
}
