#ifndef __IOT_AI_CTRL_H__
#define __IOT_AI_CTRL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot_client.h"

/**
 * @brief Try to consume a decrypted downlink payload as an AI control envelope
 *        (MQTT protocol 9000).
 *
 * If the payload is a protocol-9000 envelope and a callback is registered,
 * extracts data.data.type and data.data.data and invokes the callback, returning
 * true. Otherwise returns false (the caller should try the DP dispatcher or
 * forward to the raw message callback).
 *
 * @return true  if the payload was consumed as a protocol-9000 envelope;
 *         false otherwise.
 */
bool iot_ai_ctrl_dispatch(iot_client_t *client,
                          const uint8_t *bytes, size_t len);

#endif /* __IOT_AI_CTRL_H__ */
