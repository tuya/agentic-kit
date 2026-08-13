#ifndef IOT_AI_CTRL_H
#define IOT_AI_CTRL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iot_client.h"

bool iot_ai_ctrl_dispatch(iot_client_t *client,
                          const uint8_t *bytes, size_t len);

#endif /* IOT_AI_CTRL_H */
