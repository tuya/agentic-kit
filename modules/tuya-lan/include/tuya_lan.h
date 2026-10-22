/* Tuya LAN 3.5 device service. All calls and callbacks share one owner thread. */
#ifndef TUYA_LAN_H
#define TUYA_LAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pal.h"
#include "tuya_lan_config_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tuya_lan tuya_lan_t;

enum {
    LAN_OK = 0,
    LAN_ERR_PARAM = -1,
    LAN_ERR_MEMORY = -2,
    LAN_ERR_NET = -3,
    LAN_ERR_PROTOCOL = -4,
    LAN_ERR_AUTH = -5,
    LAN_ERR_TIMEOUT = -6,
    LAN_ERR_STATE = -8,
    LAN_ERR_LIMIT = -9,
    LAN_ERR_CRYPTO = -10,
    LAN_ERR_NOT_SUPPORTED = -11
};

#define TUYA_LAN_TCP_PORT 6668
#define TUYA_LAN_UDP_PORT 7000
#define TUYA_LAN_DISCOVERY_PORT 6667
#define TUYA_LAN_MAX_FRAME_SIZE AGENTIC_KIT_LAN_MAX_FRAME_SIZE
#define TUYA_LAN_CMD_CONTROL 0x07u
#define TUYA_LAN_CMD_REPORT 0x08u
#define TUYA_LAN_CMD_HEARTBEAT 0x09u
#define TUYA_LAN_CMD_QUERY 0x0au
#define TUYA_LAN_CMD_CONTROL_NEW 0x0du
#define TUYA_LAN_CMD_QUERY_NEW 0x10u
#define TUYA_LAN_CMD_P2P 0x20u

/* The dps object (e.g. {"1":true}) is borrowed for this callback only.
 * Return LAN_OK after applying it; any other result produces an error ACK. */
typedef int (*tuya_lan_dp_callback_t)(const char *dps_json, size_t len, void *user);
/* Write a complete DP state document {"dps":{...}}, without requiring a NUL
 * terminator. Set *len <= capacity. The service never clears DP dirty bits. */
typedef int (*tuya_lan_query_callback_t)(char *json, size_t capacity, size_t *len, void *user);
/* Opaque authenticated extension data, borrowed for the callback only. Use
 * tuya_lan_send to reply to this session. Return LAN_OK to keep the session;
 * any other result closes it. No P2P transport is implemented here. */
typedef int (*tuya_lan_command_callback_t)(uint32_t session_id, uint32_t command,
                                         const uint8_t *data, size_t len, void *user);

typedef struct {
    /* Shared SDK PAL, including its optional server/datagram callbacks. */
    const pal_t *pal;
    /* Borrowed for the entire service lifetime; local_key must be exactly
     * 16 bytes plus a terminating NUL. Destroy/recreate after credentials change. */
    const char *device_id;
    const char *uuid;
    const char *product_key;
    const char *local_key;
    const char *ipv4;
    const char *bind_ip;
    const char *broadcast_ip;
    /* Use config_init for defaults; port zero requests an ephemeral listener. */
    uint16_t tcp_port;
    uint16_t udp_port;
    uint16_t discovery_port;
    size_t max_clients;
    uint32_t handshake_timeout_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t discovery_interval_ms;
    tuya_lan_dp_callback_t on_dp;
    tuya_lan_query_callback_t on_query;
    tuya_lan_command_callback_t on_command;
    void *user;
} tuya_lan_config_t;

/* Initialize shared RNG and the process-wide cJSON/PAL allocator at startup
 * (iot_init/iot_init_default already do this). All SDK modules use the same PAL. */
void tuya_lan_config_init(tuya_lan_config_t *config);
int tuya_lan_create(const tuya_lan_config_t *config, tuya_lan_t **out);
int tuya_lan_start(tuya_lan_t *lan);
int tuya_lan_stop(tuya_lan_t *lan);
void tuya_lan_destroy(tuya_lan_t *lan);
/* Nonblocking service pump. Call frequently from the same application thread
 * as report/send. Callbacks may report/send; do not destroy/stop/process or
 * disconnect_all recursively from a callback. No worker or automatic uplink. */
int tuya_lan_process(tuya_lan_t *lan);
/* Explicitly report {"dps":{...}} to authenticated sessions. */
int tuya_lan_report(tuya_lan_t *lan, const char *json, size_t len);
int tuya_lan_send(tuya_lan_t *lan, uint32_t session_id, uint32_t command,
                  uint32_t return_code, const uint8_t *data, size_t len);
int tuya_lan_disconnect_all(tuya_lan_t *lan);
size_t tuya_lan_client_count(const tuya_lan_t *lan);
uint16_t tuya_lan_tcp_port(const tuya_lan_t *lan);
uint16_t tuya_lan_udp_port(const tuya_lan_t *lan);

#ifdef __cplusplus
}
#endif
#endif
