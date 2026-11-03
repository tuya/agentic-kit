#include "tuya_lan.h"
#include "tuya_lan_protocol.h"
#include "tuya_lan_log.h"
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

static void peer_close_socket(lan_peer_t *peer)
{
    if (!peer->socket) return;
    LAN_LOGI("session %lu closed: authenticated=%d failed=%d queued=%zu",
             (unsigned long)peer->id,
             (int)(peer->protocol && lan_protocol_authenticated(peer->protocol)),
             (int)peer->failed, peer->tx_end - peer->tx_begin);
    peer->owner->config.pal->tcp_close(peer->socket);
    peer->socket = NULL;
}

static void peer_fail(lan_peer_t *peer, int error)
{
    peer->failed = true;
    if (peer->protocol) lan_protocol_abort(peer->protocol, error);
    /* A callback may fail its own Session. Close the socket now, but keep the
     * protocol and queue alive until its active call stack has unwound. */
    peer_close_socket(peer);
}

static int peer_emit(void *user, const uint8_t *frame, size_t len)
{
    lan_peer_t *peer = user;
    if (peer->failed || !frame || !len || len > LAN_TX_CAPACITY -
                                                (peer->tx_end - peer->tx_begin)) {
        LAN_LOGW("session %lu enqueue failed: bytes=%zu queued=%zu capacity=%zu failed=%d",
                 (unsigned long)peer->id, len, peer->tx_end - peer->tx_begin,
                 (size_t)LAN_TX_CAPACITY, (int)peer->failed);
        peer_fail(peer, LAN_ERR_LIMIT);
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
    peer_close_socket(peer);
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
    if (!out) {
        LAN_LOGW("create rejected: missing output pointer");
        return LAN_ERR_PARAM;
    }
    *out = NULL;
    /* LAN maxima exclude the NUL; bounded-string capacities include it. */
    if (!c || !pal_is_valid(c->pal)) {
        LAN_LOGW("create rejected: missing config or invalid PAL");
        return LAN_ERR_PARAM;
    }
    if (!pal_has_server_network(c->pal)) {
        LAN_LOGW("create rejected: incomplete PAL server/datagram callbacks");
        return LAN_ERR_PARAM;
    }
    if (!bounded_string_nonempty(c->device_id, 64u + 1u) ||
        !bounded_string_nonempty(c->uuid, 64u + 1u) ||
        !bounded_string_nonempty(c->product_key, 64u + 1u)) {
        LAN_LOGW("create rejected: device_id, uuid or product_key missing/too long");
        return LAN_ERR_PARAM;
    }
    if (!bounded_string_nonempty(c->local_key, 16u + 1u) || strlen(c->local_key) != 16) {
        LAN_LOGW("create rejected: local_key must contain exactly 16 bytes");
        return LAN_ERR_PARAM;
    }
    if (!ipv4_valid(c->ipv4) || (c->bind_ip && !ipv4_valid(c->bind_ip)) ||
        (c->broadcast_ip && !ipv4_valid(c->broadcast_ip))) {
        LAN_LOGW("create rejected: invalid ipv4, bind_ip or broadcast_ip");
        return LAN_ERR_PARAM;
    }
    if (!c->discovery_port || !c->max_clients || c->max_clients > LAN_CLIENT_LIMIT ||
        !c->handshake_timeout_ms || !c->heartbeat_timeout_ms) {
        LAN_LOGW("create rejected: discovery_port=%u max_clients=%zu handshake_ms=%lu heartbeat_ms=%lu",
                 (unsigned)c->discovery_port, c->max_clients,
                 (unsigned long)c->handshake_timeout_ms, (unsigned long)c->heartbeat_timeout_ms);
        return LAN_ERR_PARAM;
    }
    tuya_lan_t *lan = c->pal->malloc(sizeof(*lan));
    if (!lan) {
        LAN_LOGE("create failed: service allocation (%zu bytes)", sizeof(*lan));
        return LAN_ERR_MEMORY;
    }
    memset(lan, 0, sizeof(*lan));
    lan->config = *c;
    lan->peers = c->pal->malloc(c->max_clients * sizeof(*lan->peers));
    lan->discovery = c->pal->malloc(TUYA_LAN_MAX_FRAME_SIZE);
    lan->udp_rx = c->pal->malloc(TUYA_LAN_MAX_FRAME_SIZE);
    if (!lan->peers || !lan->discovery || !lan->udp_rx) {
        LAN_LOGE("create failed: buffer allocation (peers=%zu frame_capacity=%zu)",
                 c->max_clients, (size_t)TUYA_LAN_MAX_FRAME_SIZE);
        c->pal->free(lan->peers); c->pal->free(lan->discovery);
        c->pal->free(lan->udp_rx); c->pal->free(lan);
        return LAN_ERR_MEMORY;
    }
    memset(lan->peers, 0, c->max_clients * sizeof(*lan->peers));
    for (size_t i = 0; i < c->max_clients; i++) lan->peers[i].owner = lan;
    *out = lan;
    LAN_LOGD("created: max_clients=%zu frame_capacity=%zu handshake_ms=%lu heartbeat_ms=%lu discovery_ms=%lu",
             c->max_clients, (size_t)TUYA_LAN_MAX_FRAME_SIZE,
             (unsigned long)c->handshake_timeout_ms, (unsigned long)c->heartbeat_timeout_ms,
             (unsigned long)c->discovery_interval_ms);
    return LAN_OK;
}

int tuya_lan_start(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->started || lan->processing) {
        LAN_LOGW("start rejected: started=%d processing=%d", (int)lan->started, (int)lan->processing);
        return LAN_ERR_STATE;
    }
    const tuya_lan_config_t *c = &lan->config;
    int rc = lan_discovery_build(c, lan->discovery, TUYA_LAN_MAX_FRAME_SIZE,
                                 &lan->discovery_len);
    if (rc) {
        LAN_LOGE("start failed: discovery build error=%d", rc);
        return rc;
    }
    rc = c->pal->tcp_listen(c->pal, c->bind_ip, c->tcp_port,
                           (unsigned)c->max_clients, &lan->listener);
    if (rc) {
        LAN_LOGE("start failed: tcp_listen %s:%u error=%d",
                 c->bind_ip ? c->bind_ip : "0.0.0.0", (unsigned)c->tcp_port, rc);
        goto fail;
    }
    rc = c->pal->udp_bind(c->pal, c->bind_ip, c->udp_port, true, &lan->udp);
    if (rc) {
        LAN_LOGE("start failed: udp_bind %s:%u error=%d",
                 c->bind_ip ? c->bind_ip : "0.0.0.0", (unsigned)c->udp_port, rc);
        goto fail;
    }
    rc = c->pal->socket_local_port(lan->listener, &lan->tcp_port);
    if (rc) {
        LAN_LOGE("start failed: TCP local port lookup error=%d", rc);
        goto fail;
    }
    rc = c->pal->socket_local_port(lan->udp, &lan->udp_port);
    if (rc) {
        LAN_LOGE("start failed: UDP local port lookup error=%d", rc);
        goto fail;
    }
    lan->started = true;
    lan->announced = false;
    LAN_LOGI("started: ipv4=%s bind=%s tcp=%u udp=%u discovery=%s:%u max_clients=%zu",
             c->ipv4, c->bind_ip ? c->bind_ip : "0.0.0.0",
             (unsigned)lan->tcp_port, (unsigned)lan->udp_port,
             c->broadcast_ip ? c->broadcast_ip : "disabled",
             (unsigned)c->discovery_port, c->max_clients);
    return LAN_OK;
fail:
    if (lan->listener) c->pal->tcp_close(lan->listener);
    if (lan->udp) c->pal->udp_close(lan->udp);
    lan->listener = NULL; lan->udp = NULL;
    lan->tcp_port = 0; lan->udp_port = 0;
    return LAN_ERR_NET;
}

int tuya_lan_disconnect_all(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->processing) {
        LAN_LOGW("disconnect_all rejected: called from process/callback");
        return LAN_ERR_STATE;
    }
    LAN_LOGD("disconnect_all requested");
    for (size_t i = 0; i < lan->config.max_clients; i++) peer_close(&lan->peers[i]);
    return LAN_OK;
}

int tuya_lan_stop(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (lan->processing) {
        LAN_LOGW("stop rejected: called from process/callback");
        return LAN_ERR_STATE;
    }
    if (lan->started) LAN_LOGI("stopping: tcp=%u udp=%u", (unsigned)lan->tcp_port, (unsigned)lan->udp_port);
    tuya_lan_disconnect_all(lan);
    if (lan->listener) lan->config.pal->tcp_close(lan->listener);
    if (lan->udp) lan->config.pal->udp_close(lan->udp);
    lan->listener = NULL; lan->udp = NULL;
    lan->tcp_port = 0; lan->udp_port = 0; lan->started = false;
    return LAN_OK;
}

void tuya_lan_destroy(tuya_lan_t *lan)
{
    if (!lan) return;
    if (lan->processing) {
        LAN_LOGW("destroy rejected: called from process/callback");
        return;
    }
    tuya_lan_stop(lan);
    LAN_LOGD("destroying service");
    const pal_t *pal = lan->config.pal;
    pal->free(lan->peers); pal->free(lan->discovery); pal->free(lan->udp_rx);
    pal->free(lan);
}

static void accept_clients(tuya_lan_t *lan)
{
    const tuya_lan_config_t *c = &lan->config;
    for (unsigned n = 0; n < LAN_CLIENT_LIMIT; n++) {
        void *socket = NULL;
        pal_addr_t remote = {0};
        int rc = c->pal->tcp_accept(lan->listener, &socket, &remote);
        if (rc) {
            if (rc != PAL_ERR_AGAIN) LAN_LOGW("tcp_accept failed: error=%d", rc);
            break;
        }
        lan_peer_t *peer = NULL;
        for (size_t i = 0; i < c->max_clients; i++)
            if (!lan->peers[i].socket) { peer = &lan->peers[i]; break; }
        if (!peer) {
            LAN_LOGW("connection rejected: max_clients=%zu reached", c->max_clients);
            c->pal->tcp_close(socket);
            continue;
        }
        peer->socket = socket;
        /* Session IDs are never zero; a full wrap is harmless with <=3 peers. */
        do { peer->id = ++lan->next_id; } while (!peer->id);
        LAN_LOGI("session %lu accepted from %s:%u", (unsigned long)peer->id,
                 remote.ip[0] ? remote.ip : "?", (unsigned)remote.port);
        peer->tx = c->pal->malloc(LAN_TX_CAPACITY);
        if (!peer->tx) {
            LAN_LOGE("session %lu send queue allocation failed: capacity=%zu",
                     (unsigned long)peer->id, (size_t)LAN_TX_CAPACITY);
            peer_close(peer);
            continue;
        }
        rc = lan_protocol_create(c, peer->id, peer_emit, peer, &peer->protocol);
        if (rc) {
            LAN_LOGE("session %lu protocol create failed: error=%d", (unsigned long)peer->id, rc);
            peer_close(peer);
        }
    }
}

static void flush_peer(lan_peer_t *peer)
{
    const pal_t *pal = peer->owner->config.pal;
    for (unsigned i = 0; i < LAN_IO_BUDGET && peer->tx_begin < peer->tx_end; i++) {
        size_t remain = peer->tx_end - peer->tx_begin;
        int n = pal->tcp_send(peer->socket, peer->tx + peer->tx_begin, remain, 0);
        if (n == PAL_ERR_AGAIN) return;
        if (n <= 0 || (size_t)n > remain) {
            LAN_LOGW("session %lu tcp_send failed: result=%d pending=%zu",
                     (unsigned long)peer->id, n, remain);
            peer_fail(peer, LAN_ERR_NET);
            return;
        }
        if ((size_t)n < remain)
            LAN_LOGD("session %lu partial send: sent=%d pending=%zu",
                     (unsigned long)peer->id, n, remain - (size_t)n);
        peer->tx_begin += (size_t)n;
    }
    if (peer->tx_begin == peer->tx_end) peer->tx_begin = peer->tx_end = 0;
}

static void process_peer(lan_peer_t *peer)
{
    if (peer->failed) { peer_close(peer); return; }
    int rc = lan_protocol_tick(peer->protocol);
    if (rc) { peer_fail(peer, rc); peer_close(peer); return; }
    flush_peer(peer);
    uint8_t rx[1024];
    for (unsigned i = 0; i < LAN_IO_BUDGET && !peer->failed; i++) {
        int n = peer->owner->config.pal->tcp_recv(peer->socket, rx, sizeof(rx), 0);
        if (n == PAL_ERR_AGAIN) break;
        if (!n) {
            LAN_LOGD("session %lu peer closed TCP", (unsigned long)peer->id);
            peer_fail(peer, LAN_ERR_NET);
        } else if (n < 0 || (size_t)n > sizeof(rx)) {
            LAN_LOGW("session %lu tcp_recv failed: result=%d capacity=%zu",
                     (unsigned long)peer->id, n, sizeof(rx));
            peer_fail(peer, LAN_ERR_NET);
        } else {
            rc = lan_protocol_feed(peer->protocol, rx, (size_t)n);
            if (rc) peer_fail(peer, rc);
        }
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
            if (n < 0 || (size_t)n != lan->discovery_len)
                LAN_LOGW("discovery announce failed: destination=%s:%u bytes=%zu result=%d",
                         destination.ip, (unsigned)destination.port, lan->discovery_len, n);
            else
                LAN_LOGD("discovery announced: destination=%s:%u bytes=%zu",
                         destination.ip, (unsigned)destination.port, lan->discovery_len);
            lan->announced = true;
            lan->last_discovery = now;
        }
    }
    for (unsigned i = 0; i < LAN_IO_BUDGET; i++) {
        pal_addr_t source;
        int n = c->pal->udp_recvfrom(lan->udp, &source, lan->udp_rx,
                                     TUYA_LAN_MAX_FRAME_SIZE);
        if (n == PAL_ERR_AGAIN) break;
        if (n < 0 || (size_t)n > TUYA_LAN_MAX_FRAME_SIZE) {
            LAN_LOGW("discovery udp_recvfrom failed: result=%d capacity=%zu",
                     n, (size_t)TUYA_LAN_MAX_FRAME_SIZE);
            continue;
        }
        if (!n) {
            LAN_LOGD("discovery ignored: empty datagram");
            continue;
        }
        char advertised[16];
        int rc = lan_discovery_parse(c, lan->udp_rx, (size_t)n, advertised,
                                    sizeof(advertised));
        if (rc) {
            LAN_LOGD("discovery ignored: source=%s:%u bytes=%d parse_error=%d",
                     source.ip, (unsigned)source.port, n, rc);
            continue;
        }
        if (strcmp(advertised, source.ip)) {
            LAN_LOGD("discovery ignored: source=%s:%u advertised_ip mismatch",
                     source.ip, (unsigned)source.port);
            continue;
        }
        LAN_LOGD("discovery query accepted: source=%s:%u bytes=%d",
                 source.ip, (unsigned)source.port, n);
        /* TuyaOpen answers to the app's fixed listen port (7000), not to
         * the query's source port, which may be ephemeral. */
        pal_addr_t reply = source;
        reply.port = TUYA_LAN_UDP_PORT;
        n = c->pal->udp_sendto(lan->udp, &reply, lan->discovery, lan->discovery_len);
        if (n != PAL_ERR_AGAIN && (n < 0 || (size_t)n != lan->discovery_len))
            LAN_LOGW("discovery reply failed: destination=%s:%u bytes=%zu result=%d",
                     reply.ip, (unsigned)reply.port, lan->discovery_len, n);
        else
            LAN_LOGD("discovery reply: destination=%s:%u bytes=%zu result=%d",
                     reply.ip, (unsigned)reply.port, lan->discovery_len, n);
    }
}

int tuya_lan_process(tuya_lan_t *lan)
{
    if (!lan) return LAN_ERR_PARAM;
    if (!lan->started || lan->processing) {
        LAN_LOGW("process rejected: started=%d processing=%d", (int)lan->started, (int)lan->processing);
        return LAN_ERR_STATE;
    }
    lan->processing = true;
    accept_clients(lan);
    for (size_t i = 0; i < lan->config.max_clients; i++)
        if (lan->peers[i].socket || lan->peers[i].failed) process_peer(&lan->peers[i]);
    /* A later peer's callback can fail an already-visited peer. */
    for (size_t i = 0; i < lan->config.max_clients; i++)
        if (lan->peers[i].failed) peer_close(&lan->peers[i]);
    process_discovery(lan);
    lan->processing = false;
    return LAN_OK;
}

int tuya_lan_report(tuya_lan_t *lan, const char *json, size_t len)
{
    if (!lan) return LAN_ERR_PARAM;
    if (!lan->started) {
        LAN_LOGW("report rejected: service not started");
        return LAN_ERR_STATE;
    }
    int rc = !json || !len ? LAN_ERR_PARAM :
             lan_protocol_validate_state(&lan->config, json, len);
    if (rc) {
        LAN_LOGW("report rejected: invalid DP state bytes=%zu error=%d", len, rc);
        for (size_t i = 0; i < lan->config.max_clients; i++) {
            lan_peer_t *peer = &lan->peers[i];
            if (!peer->socket || peer->failed || !lan_protocol_authenticated(peer->protocol)) continue;
            peer_fail(peer, rc);
            if (!lan->processing) peer_close(peer);
        }
        return rc;
    }
    int result = LAN_OK;
#if AGENTIC_KIT_LAN_LOG_LEVEL >= LOG_DEBUG
    size_t recipients = 0;
#endif
    for (size_t i = 0; i < lan->config.max_clients; i++) {
        lan_peer_t *peer = &lan->peers[i];
        if (!peer->socket || peer->failed || !lan_protocol_authenticated(peer->protocol)) continue;
        rc = lan_protocol_report(peer->protocol, json, len);
        if (rc) {
            LAN_LOGW("session %lu report failed: bytes=%zu error=%d", (unsigned long)peer->id, len, rc);
            peer_fail(peer, rc);
            if (!lan->processing) peer_close(peer);
            result = rc;
        }
#if AGENTIC_KIT_LAN_LOG_LEVEL >= LOG_DEBUG
        else recipients++;
#endif
    }
    LAN_LOGD("report: bytes=%zu queued_sessions=%zu result=%d", len, recipients, result);
    return result;
}

int tuya_lan_send(tuya_lan_t *lan, uint32_t session_id, uint32_t command,
                  uint32_t return_code, const uint8_t *data, size_t len)
{
    if (!lan || !session_id) return LAN_ERR_PARAM;
    if (!lan->started) {
        LAN_LOGW("send rejected: service not started");
        return LAN_ERR_STATE;
    }
    for (size_t i = 0; i < lan->config.max_clients; i++) {
        lan_peer_t *peer = &lan->peers[i];
        if (peer->socket && !peer->failed && peer->id == session_id) {
            int rc = lan_protocol_send(peer->protocol, command, return_code, data, len);
            if (rc)
                LAN_LOGW("session %lu send rejected: cmd=%lu bytes=%zu error=%d",
                         (unsigned long)session_id, (unsigned long)command, len, rc);
            if (rc) {
                peer_fail(peer, rc);
                if (!lan->processing) peer_close(peer);
            }
            return rc;
        }
    }
    LAN_LOGW("send rejected: session %lu unavailable cmd=%lu bytes=%zu",
             (unsigned long)session_id, (unsigned long)command, len);
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
