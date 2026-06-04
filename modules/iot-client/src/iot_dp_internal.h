#ifndef __IOT_DP_INTERNAL_H__
#define __IOT_DP_INTERNAL_H__

/*
 * Internal (src-private) hooks for the DP layer, used by iot_client.c and
 * iot_client_message.c. The public DP API lives in include/iot_dp.h.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iot_client.h"

/**
 * @brief (Re)build the DP registry from client->schema. Idempotent.
 *
 * NULL/empty/"[]" schema or a parse failure degrades to loose mode (empty
 * registry, passthrough dispatch) — it never fails client init. Safe to call
 * multiple times; each call fully replaces the prior registry.
 */
void iot_dp_rebuild(iot_client_t *client);

/**
 * @brief Try to consume a decrypted downlink payload as a DP envelope.
 *
 * @return true  if the payload was a recognised DP envelope and was consumed
 *               (the caller must NOT forward it to message_callback);
 *         false otherwise (caller forwards the raw payload to message_callback).
 */
bool iot_dp_dispatch_downlink(iot_client_t *client, const char *topic, size_t topic_len,
                              const uint8_t *bytes, size_t len);

/**
 * @brief Seed every value-less DP with a schema-derived default (bool=false,
 *        value=clamp(0,min,max), enum=index 0, string="", raw=empty), marking
 *        each dirty. Used only on first activation so the device has a complete
 *        initial DP state to report. NULL/loose-safe.
 */
void iot_dp_init_defaults(iot_client_t *client);

/**
 * @brief Release the DP context and all registry storage. NULL-safe; idempotent.
 */
void iot_dp_deinit(iot_client_t *client);

/**
 * @brief Resolve the ATOP host/port for this client (implemented in iot_client.c).
 *
 * Mirrors the logic in iot_client_get_session_token(): prefer parsing
 * client->https_url, else fall back to iot_region_to_host(region, env).
 * @p host_out is filled with a NUL-terminated host (empty string if none) and
 * *port_out is set (defaults to IOT_DEFAULT_PORT). Shared so iot_dp.c does not
 * duplicate parse_host_port().
 */
void iot_client_resolve_atop_host(iot_client_t *client, char *host_out, size_t host_len, uint16_t *port_out);

#endif /* __IOT_DP_INTERNAL_H__ */
