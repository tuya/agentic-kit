#include "tuya_lan.h"
#include "tuya_lan_protocol.h"
#include "bounded_string.h"

#include <string.h>

#define LAN_CLIENT_LIMIT 3u
#define LAN_IO_BUDGET 4u
#define LAN_TX_CAPACITY (2u * TUYA_LAN_MAX_FRAME_SIZE)

typedef struct {
    struct tuya_lan *owner;
    void *socket;
    lan_protocol_t *protocol;
    uint32_t id;
    bool failed;
    uint8_t *tx;
    size_t tx_begin;
    size_t tx_end;
} lan_peer_t;

struct tuya_lan {
    tuya_lan_config_t config;
    void *listener;
    void *udp;
    lan_peer_t *peers;
    uint8_t *discovery;
    uint8_t *udp_rx;
    size_t discovery_len;
    uint64_t last_discovery;
    uint32_t next_id;
    uint16_t tcp_port;
    uint16_t udp_port;
    bool started;
    bool processing;
    bool announced;
};

static bool ipv4_valid(const char *ip)
{
    if (!ip || !*ip) return false;
    for (unsigned part = 0; part < 4; part++) {
        unsigned value = 0, digits = 0;
        while (*ip >= '0' && *ip <= '9') {
            value = value * 10u + (unsigned)(*ip++ - '0');
            if (++digits > 3 || value > 255) return false;
        }
        if (!digits) return false;
        if (part == 3) return *ip == '\0';
        if (*ip++ != '.') return false;
    }
    return false;
}

void tuya_lan_config_init(tuya_lan_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->tcp_port = TUYA_LAN_TCP_PORT;
    c->udp_port = TUYA_LAN_UDP_PORT;
    c->discovery_port = TUYA_LAN_DISCOVERY_PORT;
    c->broadcast_ip = "255.255.255.255";
    c->max_clients = LAN_CLIENT_LIMIT;
    c->handshake_timeout_ms = 10000;
    c->heartbeat_timeout_ms = 30000;
    c->discovery_interval_ms = 5000;
}

static int peer_emit(void *user, const uint8_t *frame, size_t len)
{
    lan_peer_t *peer = user;
    if (peer->failed || !frame || !len || len > LAN_TX_CAPACITY -
                                                (peer->tx_end - peer->tx_begin)) {
        peer->failed = true;
        return LAN_ERR_LIMIT;
    }
    /* Compact only when the end cannot fit; partial writes preserve all bytes. */
    if (len > LAN_TX_CAPACITY - peer->tx_end) {
        memmove(peer->tx, peer->tx + peer->tx_begin, peer->tx_end - peer->tx_begin);
        peer->tx_end -= peer->tx_begin;
        peer->tx_begin = 0;
    }
    memcpy(peer->tx + peer->tx_end, frame, len);
    peer->tx_end += len;
    return LAN_OK;
}

static void peer_close(lan_peer_t *peer)
{
    const tuya_lan_config_t *c = &peer->owner->config;
    if (peer->socket) c->pal->tcp_close(peer->socket);
    if (peer->protocol) lan_protocol_destroy(peer->protocol);
    if (peer->tx) {
        /* Queued authentication frames may contain nonces/proofs. */
        volatile uint8_t *p = peer->tx;
        for (size_t i = 0; i < LAN_TX_CAPACITY; i++) p[i] = 0;
        c->pal->free(peer->tx);
    }
    struct tuya_lan *owner = peer->owner;
    memset(peer, 0, sizeof(*peer));
    peer->owner = owner;
}

int tuya_lan_create(const tuya_lan_config_t *c, tuya_lan_t **out)
{
    if (!out) return LAN_ERR_PARAM;
    *out = NULL;
    /* LAN maxima exclude the NUL; bounded-string capacities include it. */
    if (!c || !pal_is_valid(c->pal) ||
        !pal_has_server_network(c->pal) || !bounded_string_nonempty(c->device_id, 64u + 1u) ||
        !bounded_string_nonempty(c->uuid, 64u + 1u) ||
        !bounded_string_nonempty(c->product_key, 64u + 1u) ||
        !bounded_string_nonempty(c->local_key, 16u + 1u) || strlen(c->local_key) != 16 ||
        !ipv4_valid(c->ipv4) || (c->bind_ip && !ipv4_valid(c->bind_ip)) ||
        (c->broadcast_ip && !ipv4_valid(c->broadcast_ip)) ||
        !c->discovery_port || !c->max_clients || c->max_clients > LAN_CLIENT_LIMIT ||
        !c->handshake_timeout_ms || !c->heartbeat_timeout_ms)
        return LAN_ERR_PARAM;
    tuya_lan_t *lan = c->pal->malloc(sizeof(*lan));
    if (!lan) return LAN_ERR_MEMORY;
    memset(lan, 0, sizeof(*lan));
    lan->config = *c;
    lan->peers = c->pal->malloc(c->max_clients * sizeof(*lan->peers));
    lan->discovery = c->pal->malloc(TUYA_LAN_MAX_FRAME_SIZE);
    lan->udp_rx = c->pal->malloc(TUYA_LAN_MAX_FRAME_SIZE);
    if (!lan->peers || !lan->discovery || !lan->udp_rx) {
        c->pal->free(lan->peers); c->pal->free(lan->discovery);
        c->pal->free(lan->udp_rx); c->pal->free(lan);
        return LAN_ERR_MEMORY;
    }
    memset(lan->peers, 0, c->max_clients * sizeof(*lan->peers));
    for (size_t i = 0; i < c->max_clients; i++) lan->peers[i].owner = lan;
    *out = lan;
    return LAN_OK;
}

int tuya_lan_start(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->started || lan->processing) return LAN_ERR_STATE;
    const tuya_lan_config_t *c = &lan->config;
    int rc = lan_discovery_build(c, lan->discovery, TUYA_LAN_MAX_FRAME_SIZE,
                                 &lan->discovery_len);
    if (rc) return rc;
    if (c->pal->tcp_listen(c->pal, c->bind_ip, c->tcp_port,
                          (unsigned)c->max_clients, &lan->listener) ||
        c->pal->udp_bind(c->pal, c->bind_ip, c->udp_port, true, &lan->udp) ||
        c->pal->socket_local_port(lan->listener, &lan->tcp_port) ||
        c->pal->socket_local_port(lan->udp, &lan->udp_port)) {
        if (lan->listener) c->pal->tcp_close(lan->listener);
        if (lan->udp) c->pal->udp_close(lan->udp);
        lan->listener = NULL; lan->udp = NULL;
        lan->tcp_port = 0; lan->udp_port = 0;
        return LAN_ERR_NET;
    }
    lan->started = true;
    lan->announced = false;
    return LAN_OK;
}

int tuya_lan_disconnect_all(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->processing) return LAN_ERR_STATE;
    for (size_t i = 0; i < lan->config.max_clients; i++) peer_close(&lan->peers[i]);
    return LAN_OK;
}

int tuya_lan_stop(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->processing) return LAN_ERR_STATE;
    tuya_lan_disconnect_all(lan);
    if (lan->listener) lan->config.pal->tcp_close(lan->listener);
    if (lan->udp) lan->config.pal->udp_close(lan->udp);
    lan->listener = NULL; lan->udp = NULL;
    lan->tcp_port = 0; lan->udp_port = 0; lan->started = false;
    return LAN_OK;
}

void tuya_lan_destroy(tuya_lan_t *lan)
{
    if (!lan || lan->processing) return;
    tuya_lan_stop(lan);
    const pal_t *pal = lan->config.pal;
    pal->free(lan->peers); pal->free(lan->discovery); pal->free(lan->udp_rx);
    pal->free(lan);
}

static void accept_clients(tuya_lan_t *lan)
{
    const tuya_lan_config_t *c = &lan->config;
    for (unsigned n = 0; n < LAN_CLIENT_LIMIT; n++) {
        void *socket = NULL;
        int rc = c->pal->tcp_accept(lan->listener, &socket, NULL);
        if (rc) break;
        lan_peer_t *peer = NULL;
        for (size_t i = 0; i < c->max_clients; i++)
            if (!lan->peers[i].socket) { peer = &lan->peers[i]; break; }
        if (!peer) { c->pal->tcp_close(socket); continue; }
        peer->socket = socket;
        /* Session IDs are never zero; a full wrap is harmless with <=3 peers. */
        do { peer->id = ++lan->next_id; } while (!peer->id);
        peer->tx = c->pal->malloc(LAN_TX_CAPACITY);
        if (!peer->tx || lan_protocol_create(c, peer->id, peer_emit, peer,
                                              &peer->protocol)) peer_close(peer);
    }
}

static void flush_peer(lan_peer_t *peer)
{
    const pal_t *pal = peer->owner->config.pal;
    for (unsigned i = 0; i < LAN_IO_BUDGET && peer->tx_begin < peer->tx_end; i++) {
        size_t remain = peer->tx_end - peer->tx_begin;
        int n = pal->tcp_send(peer->socket, peer->tx + peer->tx_begin, remain, 0);
        if (n == PAL_ERR_AGAIN) return;
        if (n <= 0 || (size_t)n > remain) { peer->failed = true; return; }
        peer->tx_begin += (size_t)n;
    }
    if (peer->tx_begin == peer->tx_end) peer->tx_begin = peer->tx_end = 0;
}

static void process_peer(lan_peer_t *peer)
{
    if (peer->failed) { peer_close(peer); return; }
    if (lan_protocol_tick(peer->protocol)) { peer_close(peer); return; }
    flush_peer(peer);
    uint8_t rx[1024];
    for (unsigned i = 0; i < LAN_IO_BUDGET && !peer->failed; i++) {
        int n = peer->owner->config.pal->tcp_recv(peer->socket, rx, sizeof(rx), 0);
        if (n == PAL_ERR_AGAIN) break;
        if (n <= 0 || (size_t)n > sizeof(rx) ||
            lan_protocol_feed(peer->protocol, rx, (size_t)n)) peer->failed = true;
    }
    if (!peer->failed) flush_peer(peer);
    if (peer->failed) peer_close(peer);
}

static void process_discovery(tuya_lan_t *lan)
{
    const tuya_lan_config_t *c = &lan->config;
    uint64_t now = c->pal->time_ms();
    if (c->discovery_interval_ms && c->broadcast_ip &&
        (!lan->announced || now - lan->last_discovery >= c->discovery_interval_ms)) {
        pal_addr_t destination;
        memset(&destination, 0, sizeof(destination));
        memcpy(destination.ip, c->broadcast_ip, strlen(c->broadcast_ip) + 1);
        destination.port = c->discovery_port;
        int n = c->pal->udp_sendto(lan->udp, &destination, lan->discovery,
                                   lan->discovery_len);
        if (n != PAL_ERR_AGAIN) {
            lan->announced = true;
            lan->last_discovery = now;
        }
    }
    for (unsigned i = 0; i < LAN_IO_BUDGET; i++) {
        pal_addr_t source;
        int n = c->pal->udp_recvfrom(lan->udp, &source, lan->udp_rx,
                                     TUYA_LAN_MAX_FRAME_SIZE);
        if (n == PAL_ERR_AGAIN) break;
        if (n <= 0 || (size_t)n > TUYA_LAN_MAX_FRAME_SIZE) continue;
        char advertised[16];
        if (lan_discovery_parse(c, lan->udp_rx, (size_t)n, advertised,
                                 sizeof(advertised)) == LAN_OK &&
            !strcmp(advertised, source.ip))
            c->pal->udp_sendto(lan->udp, &source, lan->discovery, lan->discovery_len);
    }
}

int tuya_lan_process(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (!lan->started || lan->processing) return LAN_ERR_STATE;
    lan->processing = true;
    accept_clients(lan);
    for (size_t i = 0; i < lan->config.max_clients; i++)
        if (lan->peers[i].socket) process_peer(&lan->peers[i]);
    process_discovery(lan);
    lan->processing = false;
    return LAN_OK;
}

int tuya_lan_report(tuya_lan_t *lan, const char *json, size_t len)
{
    if (!lan || !json || !len) return LAN_ERR_PARAM;
    if (!lan->started) return LAN_ERR_STATE;
    int rc = lan_protocol_validate_state(&lan->config, json, len);
    if (rc) return rc;
    int result = LAN_OK;
    for (size_t i = 0; i < lan->config.max_clients; i++) {
        lan_peer_t *peer = &lan->peers[i];
        if (!peer->socket || peer->failed || !lan_protocol_authenticated(peer->protocol)) continue;
        rc = lan_protocol_report(peer->protocol, json, len);
        if (rc) { peer->failed = true; result = rc; }
    }
    return result;
}

int tuya_lan_send(tuya_lan_t *lan, uint32_t session_id, uint32_t command,
                  uint32_t return_code, const uint8_t *data, size_t len)
{
    if (!lan || (!data && len) || !session_id) return LAN_ERR_PARAM;
    if (!lan->started) return LAN_ERR_STATE;
    for (size_t i = 0; i < lan->config.max_clients; i++) {
        lan_peer_t *peer = &lan->peers[i];
        if (peer->socket && !peer->failed && peer->id == session_id) {
            int rc = lan_protocol_send(peer->protocol, command, return_code, data, len);
            if (rc && rc != LAN_ERR_PARAM && rc != LAN_ERR_AUTH) peer->failed = true;
            return rc;
        }
    }
    return LAN_ERR_STATE;
}

size_t tuya_lan_client_count(const tuya_lan_t *lan)
{
    size_t count = 0;
    if (lan) for (size_t i = 0; i < lan->config.max_clients; i++)
        if (lan->peers[i].socket && !lan->peers[i].failed) count++;
    return count;
}
uint16_t tuya_lan_tcp_port(const tuya_lan_t *lan) { return lan ? lan->tcp_port : 0; }
uint16_t tuya_lan_udp_port(const tuya_lan_t *lan) { return lan ? lan->udp_port : 0; }
