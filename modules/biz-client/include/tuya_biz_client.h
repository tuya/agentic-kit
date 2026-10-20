#ifndef TUYA_BIZ_CLIENT_H
#define TUYA_BIZ_CLIENT_H

#include "iot_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Typed device-side cloud services, over the public IoT ATOP API.
 * Call iot_init()/iot_init_default() first. The IoT client and its PAL are
 * borrowed and must outlive this context and every result allocated by it.
 * Calls are synchronous; serialize calls and destruction in an app-owned
 * worker, never in a UI/MQTT/TAI receive callback. No task, MQTT connection,
 * cache, retry, or TAI Session is created or managed here. */
typedef struct tuya_biz_client tuya_biz_client_t;

/** request_lang controls display labels, NOT the role's reply language.
 * NULL defaults to "zh"; nonempty UTF-8 strings of at most 31 bytes accepted.
 * The client must be activated before performing cloud operations. */
int tuya_biz_client_create(iot_client_t *iot, const char *request_lang,
                           tuya_biz_client_t **out);
void tuya_biz_client_destroy(tuya_biz_client_t *client);

#ifdef __cplusplus
}
#endif
#endif
