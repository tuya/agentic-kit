#ifndef TUYA_LAN_PROTOCOL_H
#define TUYA_LAN_PROTOCOL_H
#include "tuya_lan.h"

typedef struct lan_protocol lan_protocol_t;
/* Must COPY frame bytes before returning. Nonzero return closes this session. */
typedef int (*lan_protocol_emit_t)(void *user, const uint8_t *frame, size_t len);
int lan_protocol_create(const tuya_lan_config_t *config, uint32_t session_id,
                        lan_protocol_emit_t emit, void *user, lan_protocol_t **out);
void lan_protocol_destroy(lan_protocol_t *protocol);
int lan_protocol_feed(lan_protocol_t *protocol, const uint8_t *bytes, size_t len);
int lan_protocol_tick(lan_protocol_t *protocol);
bool lan_protocol_authenticated(const lan_protocol_t *protocol);
int lan_protocol_report(lan_protocol_t *protocol, const char *json, size_t len);
int lan_protocol_send(lan_protocol_t *protocol, uint32_t command, uint32_t return_code,
                      const uint8_t *data, size_t len);
/* Both functions use caller-owned bounded buffers. Discovery input is verified
 * with the published discovery key, never trusted as a network destination.
 * parse returns the advertised IPv4 only for source-address comparison. */
int lan_discovery_build(const tuya_lan_config_t *config, uint8_t *out,
                        size_t capacity, size_t *len);
int lan_discovery_parse(const tuya_lan_config_t *config, const uint8_t *data,
                        size_t len, char *ipv4, size_t capacity);
int lan_protocol_validate_state(const tuya_lan_config_t *config, const char *json,
                                size_t len);
#endif
