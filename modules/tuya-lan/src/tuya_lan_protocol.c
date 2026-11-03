/* TuyaOpen LAN 3.5 wire-compatible framing and device-side session protocol.
 * The service owns the sockets; this file owns only authenticated messages. */
#include "tuya_lan_protocol.h"
#include "tuya_lan_log.h"
#include "rng.h"
#include "json_lexical.h"
#include "cJSON.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/md5.h"
#include "mbedtls/platform_util.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FRAME_HEADER 18u
#define FRAME_OVERHEAD 50u
#define INNER_HEADER 15u
#define RETURN_CODE 4u
#define MAX_PLAINTEXT (TUYA_LAN_MAX_FRAME_SIZE - FRAME_OVERHEAD)
#define CMD_SECURITY_START 3u
#define CMD_SECURITY_RESPONSE 4u
#define CMD_SECURITY_FINISH 5u
/* TuyaOpen 68f76c9 answers discovery with FRM_TYPE_ENCRYPTION (0x13);
 * the App query arrives as FRM_TYPE_APP_UDP_BOARDCAST (0x25). Optional
 * agentic-kit announcements reuse the discovery response format. */
#define CMD_DISCOVERY 0x13u
#define CMD_DISCOVERY_QUERY 0x25u

struct lan_protocol {
    tuya_lan_config_t config;
    uint32_t id;
    lan_protocol_emit_t emit;
    void *emit_user;
    uint8_t local_key[16];
    uint8_t session_key[16];
    uint8_t rand_a[16];
    uint8_t rand_b[16];
    uint8_t rx[TUYA_LAN_MAX_FRAME_SIZE];
    size_t used;
    uint32_t tx_sequence;
    uint32_t rx_sequence;
    bool have_rx_sequence;
    bool authenticated;
    unsigned int handshake_state;
    int error;
    uint64_t created;
    uint64_t last_message;
};

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}

static int fail(lan_protocol_t *p, int error)
{
    if (!p->error) {
        p->error = error;
        p->authenticated = false;
        mbedtls_platform_zeroize(p->session_key, sizeof(p->session_key));
        LAN_LOGW("session %lu protocol failed: rc=%d handshake=%u buffered=%zu",
                 (unsigned long)p->id, error, p->handshake_state, p->used);
    }
    return p->error;
}

int lan_protocol_abort(lan_protocol_t *p, int error)
{
    if (!p) return LAN_ERR_PARAM;
    return fail(p, error < 0 ? error : LAN_ERR_PROTOCOL);
}

static int hmac(const uint8_t key[16], const uint8_t random[16], uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md && mbedtls_md_hmac(md, key, 16, random, 16, out) == 0 ? LAN_OK : LAN_ERR_CRYPTO;
}

static bool equal_secret(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t different = 0;
    size_t i;
    for (i = 0; i < n; ++i) different |= (uint8_t)(a[i] ^ b[i]);
    return different == 0;
}

static int encrypt(const pal_t *pal, const uint8_t key[16], uint32_t sequence,
                   uint32_t command, const uint8_t *data, size_t len,
                   uint8_t *out, size_t capacity, size_t *out_len)
{
    mbedtls_gcm_context gcm;
    int result;
    if (!out || !out_len || (!data && len)) return LAN_ERR_PARAM;
    if (len > MAX_PLAINTEXT || capacity < FRAME_OVERHEAD + len) return LAN_ERR_LIMIT;
    memset(out, 0, FRAME_HEADER);
    put32(out, 0x6699u);
    put32(out + 6, sequence);
    put32(out + 10, command);
    put32(out + 14, (uint32_t)(28u + len));
    if (rng_bytes(pal, out + FRAME_HEADER, 12) != 0) return LAN_ERR_CRYPTO;
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!result) result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                           len, out + FRAME_HEADER, 12, out + 4, 14,
                           data, out + FRAME_HEADER + 12, 16,
                           out + FRAME_HEADER + 12 + len);
    mbedtls_gcm_free(&gcm);
    if (result) return LAN_ERR_CRYPTO;
    put32(out + FRAME_HEADER + 28 + len, 0x9966u);
    *out_len = FRAME_OVERHEAD + len;
    return LAN_OK;
}

static int decrypt(const uint8_t key[16], const uint8_t *frame, size_t len,
                   uint8_t *out, size_t *out_len)
{
    mbedtls_gcm_context gcm;
    uint32_t length;
    size_t plaintext;
    int result;
    if (len < FRAME_OVERHEAD || len > TUYA_LAN_MAX_FRAME_SIZE ||
        get32(frame) != 0x6699u || frame[4] || frame[5]) return LAN_ERR_PROTOCOL;
    length = get32(frame + 14);
    if (length < 28u || length > TUYA_LAN_MAX_FRAME_SIZE - FRAME_HEADER - 4u ||
        len != (size_t)length + FRAME_HEADER + 4u ||
        get32(frame + len - 4u) != 0x9966u) return LAN_ERR_PROTOCOL;
    plaintext = length - 28u;
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (!result) result = mbedtls_gcm_auth_decrypt(&gcm, plaintext, frame + FRAME_HEADER,
                         12, frame + 4, 14, frame + FRAME_HEADER + 12 + plaintext,
                         16, frame + FRAME_HEADER + 12, out);
    mbedtls_gcm_free(&gcm);
    if (result) return LAN_ERR_AUTH;
    *out_len = plaintext;
    return LAN_OK;
}

static bool json_finite(const cJSON *node)
{
    const cJSON *child;
    if (cJSON_IsNumber(node) && !isfinite(node->valuedouble)) return false;
    for (child = node->child; child; child = child->next)
        if (!json_finite(child)) return false;
    return true;
}

/* Explicit length plus a copied terminator prevents cJSON looking into an
 * adjacent frame. Exact parse-end rejects embedded NULs and trailing garbage. */
static cJSON *parse_json(const tuya_lan_config_t *config, const char *data, size_t len)
{
    char *copy;
    const char *end = NULL;
    cJSON *root;
    if (!data || !len || len > MAX_PLAINTEXT || !json_lexical_validate(data, len)) return NULL;
    copy = config->pal->malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, data, len);
    copy[len] = 0;
    root = cJSON_ParseWithLengthOpts(copy, len + 1, &end, true);
    /* Printing a nonfinite cJSON number produces JSON null. Reject values the
     * parser cannot represent before that conversion can change a DP command. */
    if (!root || end != copy + len || !json_finite(root)) {
        cJSON_Delete(root);
        root = NULL;
    }
    config->pal->free(copy);
    return root;
}

static cJSON *unique_member(cJSON *root, const char *name)
{
    cJSON *child, *found = NULL;
    if (!cJSON_IsObject(root)) return NULL;
    cJSON_ArrayForEach(child, root) {
        if (child->string && strcmp(child->string, name) == 0) {
            if (found) return NULL;
            found = child;
        }
    }
    return found;
}

static bool state_valid(cJSON *root)
{
    return cJSON_IsObject(root) && cJSON_IsObject(unique_member(root, "dps"));
}

int lan_protocol_validate_state(const tuya_lan_config_t *config, const char *json, size_t len)
{
    cJSON *root;
    bool valid;
    if (!config || !config->pal || !config->pal->malloc || !config->pal->free)
        return LAN_ERR_PARAM;
    root = parse_json(config, json, len);
    valid = state_valid(root);
    cJSON_Delete(root);
    return valid ? LAN_OK : LAN_ERR_PARAM;
}

static int send_frame(lan_protocol_t *p, uint32_t command, uint32_t return_code,
                      const uint8_t *data, size_t len, const uint8_t key[16])
{
    uint8_t *frame, *plaintext;
    size_t frame_len = 0;
    int result;
    if (p->error) return p->error;
    if ((!data && len) || len > MAX_PLAINTEXT - RETURN_CODE) {
        LAN_LOGW("session %lu send rejected: cmd=0x%lx bytes=%zu limit=%zu",
                 (unsigned long)p->id, (unsigned long)command, len,
                 (size_t)(MAX_PLAINTEXT - RETURN_CODE));
        return fail(p, LAN_ERR_LIMIT);
    }
    if (p->tx_sequence == UINT32_MAX) {
        LAN_LOGW("session %lu transmit sequence exhausted", (unsigned long)p->id);
        return fail(p, LAN_ERR_LIMIT);
    }
    frame = p->config.pal->malloc(FRAME_OVERHEAD + RETURN_CODE + len);
    plaintext = p->config.pal->malloc(RETURN_CODE + len);
    if (!frame || !plaintext) {
        LAN_LOGE("session %lu send buffer allocation failed: cmd=0x%lx bytes=%zu",
                 (unsigned long)p->id, (unsigned long)command, len);
        p->config.pal->free(frame); p->config.pal->free(plaintext);
        return fail(p, LAN_ERR_MEMORY);
    }
    put32(plaintext, return_code);
    if (len) memcpy(plaintext + RETURN_CODE, data, len);
    LAN_LOGD("session %lu sending: cmd=0x%lx seq=%lu bytes=%zu return_code=%lu",
             (unsigned long)p->id, (unsigned long)command,
             (unsigned long)p->tx_sequence, len, (unsigned long)return_code);
    result = encrypt(p->config.pal, key, p->tx_sequence++, command, plaintext,
                     RETURN_CODE + len, frame, FRAME_OVERHEAD + RETURN_CODE + len,
                     &frame_len);
    if (result) {
        LAN_LOGE("session %lu frame encryption failed: cmd=0x%lx rc=%d",
                 (unsigned long)p->id, (unsigned long)command, result);
    } else {
        result = p->emit(p->emit_user, frame, frame_len);
        if (result)
            LAN_LOGW("session %lu frame queue failed: cmd=0x%lx bytes=%zu rc=%d",
                     (unsigned long)p->id, (unsigned long)command, frame_len, result);
    }
    mbedtls_platform_zeroize(plaintext, RETURN_CODE + len);
    p->config.pal->free(plaintext); p->config.pal->free(frame);
    return result ? fail(p, result < 0 ? result : LAN_ERR_NET) : LAN_OK;
}

int lan_protocol_send(lan_protocol_t *p, uint32_t command, uint32_t return_code,
                      const uint8_t *data, size_t len)
{
    if (!p) return LAN_ERR_PARAM;
    if (p->error) return p->error;
    if (!data && len) return fail(p, LAN_ERR_PARAM);
    if (!p->authenticated) {
        LAN_LOGW("session %lu send rejected before authentication: cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
        return fail(p, LAN_ERR_STATE);
    }
    if (command == CMD_SECURITY_START || command == CMD_SECURITY_RESPONSE ||
        command == CMD_SECURITY_FINISH) {
        LAN_LOGW("session %lu application send rejected: security cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
        return fail(p, LAN_ERR_PARAM);
    }
    return send_frame(p, command, return_code, data, len, p->session_key);
}

int lan_protocol_report(lan_protocol_t *p, const char *json, size_t len)
{
    uint8_t *payload;
    char prefix[96];
    int prefix_len, result;
    size_t payload_len;
    if (!p) return LAN_ERR_PARAM;
    if (p->error) return p->error;
    if (!p->authenticated) return fail(p, LAN_ERR_STATE);
    if (lan_protocol_validate_state(&p->config, json, len) != LAN_OK) {
        LAN_LOGW("session %lu report rejected: invalid DP state bytes=%zu",
                 (unsigned long)p->id, len);
        return fail(p, LAN_ERR_PARAM);
    }
    prefix_len = snprintf(prefix, sizeof(prefix), "{\"protocol\":4,\"t\":%llu,\"data\":",
                         (unsigned long long)(p->config.pal->wal_clock_time() / 1000u));
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(prefix)) {
        LAN_LOGE("session %lu report envelope formatting failed", (unsigned long)p->id);
        return fail(p, LAN_ERR_LIMIT);
    }
    payload_len = INNER_HEADER + (size_t)prefix_len + len + 1u;
    if (payload_len > MAX_PLAINTEXT - RETURN_CODE) {
        LAN_LOGW("session %lu report too large: bytes=%zu limit=%zu",
                 (unsigned long)p->id, payload_len, (size_t)(MAX_PLAINTEXT - RETURN_CODE));
        return fail(p, LAN_ERR_LIMIT);
    }
    payload = p->config.pal->malloc(payload_len);
    if (!payload) {
        LAN_LOGE("session %lu report buffer allocation failed: bytes=%zu",
                 (unsigned long)p->id, payload_len);
        return fail(p, LAN_ERR_MEMORY);
    }
    memset(payload, 0, INNER_HEADER);
    memcpy(payload, "3.5", 3);
    put32(payload + 7, p->tx_sequence);
    put32(payload + 11, 1);
    memcpy(payload + INNER_HEADER, prefix, (size_t)prefix_len);
    memcpy(payload + INNER_HEADER + prefix_len, json, len);
    payload[payload_len - 1] = '}';
    result = lan_protocol_send(p, TUYA_LAN_CMD_REPORT, 0, payload, payload_len);
    p->config.pal->free(payload);
    return result;
}

static int control(lan_protocol_t *p, uint32_t command, const uint8_t *data, size_t len)
{
    cJSON *root = NULL, *body, *dps;
    char *dps_text = NULL;
    int status = LAN_ERR_PROTOCOL;
    if (len <= INNER_HEADER || memcmp(data, "3.5", 3) != 0) {
        LAN_LOGW("session %lu control rejected: invalid inner header cmd=0x%lx bytes=%zu",
                 (unsigned long)p->id, (unsigned long)command, len);
        goto done;
    }
    root = parse_json(&p->config, (const char *)data + INNER_HEADER, len - INNER_HEADER);
    body = unique_member(root, "data");
    dps = unique_member(body, "dps");
    if (!cJSON_IsObject(dps)) {
        LAN_LOGW("session %lu control rejected: invalid data/dps object cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
        goto done;
    }
    if (!p->config.on_dp) {
        LAN_LOGW("session %lu control rejected: no DP callback cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
        status = LAN_ERR_NOT_SUPPORTED;
        goto done;
    }
    dps_text = cJSON_PrintUnformatted(dps);
    if (!dps_text) {
        LAN_LOGE("session %lu control DP serialization failed", (unsigned long)p->id);
        status = LAN_ERR_MEMORY;
        goto done;
    }
    LAN_LOGD("session %lu control callback: cmd=0x%lx request_bytes=%zu",
             (unsigned long)p->id, (unsigned long)command, len);
    status = p->config.on_dp(dps_text, strlen(dps_text), p->config.user);
    if (p->error) status = p->error;
    if (status)
        LAN_LOGW("session %lu control callback rejected: cmd=0x%lx rc=%d",
                 (unsigned long)p->id, (unsigned long)command, status);
    else
        LAN_LOGD("session %lu control applied: cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
 done:
    cJSON_free(dps_text); cJSON_Delete(root);
    /* Only accepted controls receive an ACK. Every rejection ends the Session. */
    return status ? lan_protocol_abort(p, status)
                  : lan_protocol_send(p, command, 0, NULL, 0);
}

static int query(lan_protocol_t *p, uint32_t command, const uint8_t *data, size_t len)
{
    cJSON *request, *state = NULL;
    char *output, *response = NULL;
    size_t out_len = 0;
    int result;
    /* Query payloads are plain JSON in TuyaOpen; tolerate the LPV35 inner
     * header used by some applications without accepting arbitrary prefixes. */
    if (len >= INNER_HEADER && memcmp(data, "3.5", 3) == 0) {
        data += INNER_HEADER; len -= INNER_HEADER;
    }
    LAN_LOGD("session %lu query: cmd=0x%lx bytes=%zu",
             (unsigned long)p->id, (unsigned long)command, len);
    request = parse_json(&p->config, (const char *)data, len);
    if (!cJSON_IsObject(request) || !p->config.on_query) {
        if (!cJSON_IsObject(request))
            LAN_LOGW("session %lu query rejected: invalid request object cmd=0x%lx",
                     (unsigned long)p->id, (unsigned long)command);
        else
            LAN_LOGW("session %lu query rejected: no query callback cmd=0x%lx",
                     (unsigned long)p->id, (unsigned long)command);
        int error = cJSON_IsObject(request) ? LAN_ERR_NOT_SUPPORTED : LAN_ERR_PROTOCOL;
        cJSON_Delete(request);
        return fail(p, error);
    }
    cJSON_Delete(request);
    output = p->config.pal->malloc(MAX_PLAINTEXT - RETURN_CODE);
    if (!output) {
        LAN_LOGE("session %lu query buffer allocation failed: bytes=%zu",
                 (unsigned long)p->id, (size_t)(MAX_PLAINTEXT - RETURN_CODE));
        return fail(p, LAN_ERR_MEMORY);
    }
    result = p->config.on_query(output, MAX_PLAINTEXT - RETURN_CODE, &out_len, p->config.user);
    if (p->error) { result = p->error; goto done; }
    if (!result && out_len <= MAX_PLAINTEXT - RETURN_CODE)
        state = parse_json(&p->config, output, out_len);
    if (!result && state_valid(state))
    {
        /* Identity belongs to this service, not to the application's DP state.
         * Replace every callback-supplied devId before serializing the reply. */
        while (cJSON_GetObjectItemCaseSensitive(state, "devId"))
            cJSON_DeleteItemFromObjectCaseSensitive(state, "devId");
        if (!cJSON_AddStringToObject(state, "devId", p->config.device_id)) {
            LAN_LOGE("session %lu query identity allocation failed", (unsigned long)p->id);
            result = fail(p, LAN_ERR_MEMORY);
            goto done;
        }
        response = cJSON_PrintUnformatted(state);
        if (!response) {
            LAN_LOGE("session %lu query reply serialization failed", (unsigned long)p->id);
            result = fail(p, LAN_ERR_MEMORY);
            goto done;
        }
        out_len = strlen(response);
        if (out_len > MAX_PLAINTEXT - RETURN_CODE) {
            LAN_LOGW("session %lu query reply too large after adding identity: bytes=%zu capacity=%zu",
                     (unsigned long)p->id, out_len, (size_t)(MAX_PLAINTEXT - RETURN_CODE));
            result = fail(p, LAN_ERR_LIMIT);
            goto done;
        }
        LAN_LOGD("session %lu query callback complete: cmd=0x%lx bytes=%zu",
                 (unsigned long)p->id, (unsigned long)command, out_len);
        result = lan_protocol_send(p, command, 0, (const uint8_t *)response, out_len);
    } else {
        LAN_LOGW("session %lu query callback failed or returned invalid DP state: cmd=0x%lx rc=%d bytes=%zu capacity=%zu",
                 (unsigned long)p->id, (unsigned long)command, result, out_len,
                 (size_t)(MAX_PLAINTEXT - RETURN_CODE));
        if (!result)
            result = out_len > MAX_PLAINTEXT - RETURN_CODE ? LAN_ERR_LIMIT : LAN_ERR_PROTOCOL;
    }
 done:
    cJSON_free(response);
    cJSON_Delete(state);
    p->config.pal->free(output);
    return result ? lan_protocol_abort(p, result) : p->error;
}

static int handshake(lan_protocol_t *p, uint32_t command, const uint8_t *data, size_t len)
{
    uint8_t response[48], expected[32], mixed[16], tag[16];
    mbedtls_gcm_context gcm;
    int result = LAN_ERR_AUTH;
    size_t i;
    if (command == CMD_SECURITY_START) {
        if (p->handshake_state || p->authenticated) {
            LAN_LOGW("session %lu handshake start out of order: state=%u authenticated=%u",
                     (unsigned long)p->id, p->handshake_state, (unsigned)p->authenticated);
            return fail(p, LAN_ERR_AUTH);
        }
        /* TuyaOpen only requires >= 16 bytes and reads the first 16. */
        if (len < 16u) {
            LAN_LOGW("session %lu start payload %zu bytes (<16)", (unsigned long)p->id, len);
            return fail(p, LAN_ERR_AUTH);
        }
        memcpy(p->rand_a, data, 16);
        if (rng_bytes(p->config.pal, p->rand_b, 16) != 0) {
            LAN_LOGE("session %lu handshake entropy failed", (unsigned long)p->id);
            return fail(p, LAN_ERR_CRYPTO);
        }
        memcpy(response, p->rand_b, 16);
        if (hmac(p->local_key, p->rand_a, response + 16)) {
            LAN_LOGE("session %lu handshake response HMAC failed", (unsigned long)p->id);
            return fail(p, LAN_ERR_CRYPTO);
        }
        p->handshake_state = 1;
        result = send_frame(p, CMD_SECURITY_RESPONSE, 0, response, sizeof(response), p->local_key);
        if (!result)
            LAN_LOGD("session %lu handshake response queued; awaiting finish",
                     (unsigned long)p->id);
        mbedtls_platform_zeroize(response, sizeof(response));
        return result;
    }
    if (command != CMD_SECURITY_FINISH || p->handshake_state != 1 || p->authenticated) {
        LAN_LOGW("session %lu handshake finish out of order: cmd=0x%lx state=%u authenticated=%u",
                 (unsigned long)p->id, (unsigned long)command,
                 p->handshake_state, (unsigned)p->authenticated);
        return fail(p, LAN_ERR_AUTH);
    }
    /* TuyaOpen only requires >= 32 bytes and reads the first 32. */
    if (len < 32u) {
        LAN_LOGW("session %lu finish payload %zu bytes (<32)", (unsigned long)p->id, len);
        return fail(p, LAN_ERR_AUTH);
    }
    if (hmac(p->local_key, p->rand_b, expected)) {
        LAN_LOGE("session %lu handshake proof HMAC failed", (unsigned long)p->id);
        return fail(p, LAN_ERR_CRYPTO);
    }
    if (!equal_secret(expected, data, sizeof(expected))) {
        LAN_LOGW("session %lu finish hmac mismatch (peer has a different local_key?)",
                 (unsigned long)p->id);
        goto done;
    }
    for (i = 0; i < 16; ++i) mixed[i] = (uint8_t)(p->rand_a[i] ^ p->rand_b[i]);
    mbedtls_gcm_init(&gcm);
    result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, p->local_key, 128);
    if (!result) result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 16,
                         p->rand_a, 12, NULL, 0, mixed, p->session_key, 16, tag);
    mbedtls_gcm_free(&gcm);
    if (result) {
        LAN_LOGE("session %lu session-key derivation failed: crypto_rc=%d",
                 (unsigned long)p->id, result);
        result = LAN_ERR_CRYPTO;
    }
    else {
        p->authenticated = true;
        p->handshake_state = 2;
        result = LAN_OK;
        LAN_LOGI("session %lu authenticated", (unsigned long)p->id);
    }
 done:
    mbedtls_platform_zeroize(expected, sizeof(expected));
    mbedtls_platform_zeroize(mixed, sizeof(mixed));
    mbedtls_platform_zeroize(tag, sizeof(tag));
    if (result) return fail(p, result);
    mbedtls_platform_zeroize(p->rand_a, sizeof(p->rand_a));
    mbedtls_platform_zeroize(p->rand_b, sizeof(p->rand_b));
    return LAN_OK;
}

static int process_frame(lan_protocol_t *p, const uint8_t *frame, size_t len)
{
    uint8_t *plaintext;
    uint32_t command = get32(frame + 10), sequence = get32(frame + 6);
    const uint8_t *key = p->session_key;
    size_t plain_len = 0;
    int result;
    if (p->error) return p->error;
    if (command == CMD_SECURITY_RESPONSE) {
        LAN_LOGW("session %lu unexpected security response", (unsigned long)p->id);
        return fail(p, LAN_ERR_AUTH);
    }
    if (command == CMD_SECURITY_START || command == CMD_SECURITY_FINISH) {
        key = p->local_key;
    }
    else if (!p->authenticated) {
        LAN_LOGW("session %lu command before authentication: cmd=0x%lx",
                 (unsigned long)p->id, (unsigned long)command);
        return fail(p, LAN_ERR_AUTH);
    }
    plaintext = p->config.pal->malloc(len - FRAME_OVERHEAD + 1);
    if (!plaintext) {
        LAN_LOGE("session %lu receive buffer allocation failed: frame_bytes=%zu",
                 (unsigned long)p->id, len);
        return fail(p, LAN_ERR_MEMORY);
    }
    result = decrypt(key, frame, len, plaintext, &plain_len);
    if (result) {
        if (result == LAN_ERR_AUTH)
            LAN_LOGW("session %lu decrypt auth fail cmd=%lu frame=%zu (peer used a different key?)",
                     (unsigned long)p->id, (unsigned long)command, len);
        else
            LAN_LOGW("session %lu invalid frame geometry: cmd=0x%lx seq=%lu bytes=%zu rc=%d",
                     (unsigned long)p->id, (unsigned long)command,
                     (unsigned long)sequence, len, result);
        goto done;
    }
    if (p->have_rx_sequence && sequence <= p->rx_sequence) {
        LAN_LOGW("session %lu replay/out-of-order frame: cmd=0x%lx seq=%lu previous=%lu",
                 (unsigned long)p->id, (unsigned long)command,
                 (unsigned long)sequence, (unsigned long)p->rx_sequence);
        result = LAN_ERR_AUTH;
        goto done;
    }
    LAN_LOGD("session %lu received: cmd=0x%lx seq=%lu bytes=%zu",
             (unsigned long)p->id, (unsigned long)command,
             (unsigned long)sequence, plain_len);
    /* Never commit an unauthenticated sequence number. */
    p->rx_sequence = sequence; p->have_rx_sequence = true;
    p->last_message = p->config.pal->time_ms();
    if (command == CMD_SECURITY_START || command == CMD_SECURITY_FINISH)
        result = handshake(p, command, plaintext, plain_len);
    else if (command == TUYA_LAN_CMD_CONTROL || command == TUYA_LAN_CMD_CONTROL_NEW)
        result = control(p, command, plaintext, plain_len);
    else if (command == TUYA_LAN_CMD_QUERY || command == TUYA_LAN_CMD_QUERY_NEW)
        result = query(p, command, plaintext, plain_len);
    else if (command == TUYA_LAN_CMD_HEARTBEAT)
        result = lan_protocol_send(p, command, 0, NULL, 0);
    else if (p->config.on_command) {
        result = p->config.on_command(p->id, command, plaintext, plain_len, p->config.user);
        if (result)
            LAN_LOGW("session %lu extension callback failed: cmd=0x%lx bytes=%zu rc=%d",
                     (unsigned long)p->id, (unsigned long)command, plain_len, result);
        else
            LAN_LOGD("session %lu extension handled: cmd=0x%lx bytes=%zu",
                     (unsigned long)p->id, (unsigned long)command, plain_len);
    } else {
        LAN_LOGW("session %lu unsupported command: cmd=0x%lx bytes=%zu",
                 (unsigned long)p->id, (unsigned long)command, plain_len);
        result = LAN_ERR_NOT_SUPPORTED;
    }
 done:
    mbedtls_platform_zeroize(plaintext, len - FRAME_OVERHEAD + 1);
    p->config.pal->free(plaintext);
    return result ? lan_protocol_abort(p, result) : p->error;
}

int lan_protocol_create(const tuya_lan_config_t *config, uint32_t id,
                        lan_protocol_emit_t emit, void *user, lan_protocol_t **out)
{
    lan_protocol_t *p;
    uint8_t random[2];
    if (out) *out = NULL;
    if (!config || !out || !emit || !config->pal || !config->pal->malloc ||
        !config->pal->free || !config->pal->time_ms || !config->pal->wal_clock_time ||
        !config->local_key || strlen(config->local_key) != 16u) return LAN_ERR_PARAM;
    if (rng_bytes(config->pal, random, sizeof(random)) != 0) {
        LAN_LOGE("session %lu protocol creation entropy failed", (unsigned long)id);
        return LAN_ERR_CRYPTO;
    }
    p = config->pal->malloc(sizeof(*p));
    if (!p) {
        LAN_LOGE("session %lu protocol allocation failed: bytes=%zu",
                 (unsigned long)id, sizeof(*p));
        return LAN_ERR_MEMORY;
    }
    memset(p, 0, sizeof(*p));
    p->config = *config; p->id = id; p->emit = emit; p->emit_user = user;
    memcpy(p->local_key, config->local_key, 16);
    p->tx_sequence = ((uint32_t)random[0] << 8) | random[1];
    p->created = p->last_message = config->pal->time_ms();
    *out = p;
    LAN_LOGD("session %lu protocol ready: handshake_timeout_ms=%lu heartbeat_timeout_ms=%lu",
             (unsigned long)id, (unsigned long)config->handshake_timeout_ms,
             (unsigned long)config->heartbeat_timeout_ms);
    return LAN_OK;
}

void lan_protocol_destroy(lan_protocol_t *p)
{
    const pal_t *pal;
    if (!p) return;
    pal = p->config.pal;
    mbedtls_platform_zeroize(p, sizeof(*p));
    pal->free(p);
}

int lan_protocol_tick(lan_protocol_t *p)
{
    uint64_t now, since;
    uint32_t timeout;
    if (!p) return LAN_ERR_PARAM;
    if (p->error) return p->error;
    now = p->config.pal->time_ms();
    since = p->authenticated ? p->last_message : p->created;
    timeout = p->authenticated ? p->config.heartbeat_timeout_ms : p->config.handshake_timeout_ms;
    if (timeout && now - since >= timeout) {
        LAN_LOGW("session %lu %s timeout: elapsed_ms=%llu limit_ms=%lu buffered=%zu",
                 (unsigned long)p->id, p->authenticated ? "heartbeat" : "handshake",
                 (unsigned long long)(now - since), (unsigned long)timeout, p->used);
        return fail(p, LAN_ERR_TIMEOUT);
    }
    return LAN_OK;
}

bool lan_protocol_authenticated(const lan_protocol_t *p)
{
    return p && p->authenticated && !p->error;
}

int lan_protocol_feed(lan_protocol_t *p, const uint8_t *bytes, size_t len)
{
    int status;
    if (!p) return LAN_ERR_PARAM;
    if (p->error) return p->error;
    if (!bytes && len) return fail(p, LAN_ERR_PARAM);
    status = lan_protocol_tick(p);
    if (status) return status;
    while (len) {
        size_t target, amount;
        uint32_t length;
        if (p->used < FRAME_HEADER) target = FRAME_HEADER;
        else {
            length = get32(p->rx + 14);
            if (get32(p->rx) != 0x6699u || p->rx[4] || p->rx[5] || length < 28u ||
                length > TUYA_LAN_MAX_FRAME_SIZE - FRAME_HEADER - 4u) {
                LAN_LOGW("session %lu invalid frame header: length=%lu max=%zu",
                         (unsigned long)p->id, (unsigned long)length,
                         (size_t)(TUYA_LAN_MAX_FRAME_SIZE - FRAME_HEADER - 4u));
                return fail(p, LAN_ERR_PROTOCOL);
            }
            target = FRAME_HEADER + (size_t)length + 4u;
        }
        amount = target - p->used;
        if (amount > len) amount = len;
        memcpy(p->rx + p->used, bytes, amount);
        p->used += amount; bytes += amount; len -= amount;
        if (p->used >= FRAME_HEADER) {
            length = get32(p->rx + 14);
            if (get32(p->rx) != 0x6699u || p->rx[4] || p->rx[5] || length < 28u ||
                length > TUYA_LAN_MAX_FRAME_SIZE - FRAME_HEADER - 4u) {
                LAN_LOGW("session %lu invalid frame header: length=%lu max=%zu",
                         (unsigned long)p->id, (unsigned long)length,
                         (size_t)(TUYA_LAN_MAX_FRAME_SIZE - FRAME_HEADER - 4u));
                return fail(p, LAN_ERR_PROTOCOL);
            }
            target = FRAME_HEADER + (size_t)length + 4u;
            if (p->used == target) {
                int result = process_frame(p, p->rx, target);
                p->used = 0;
                if (result) return result;
            }
        }
    }
    return LAN_OK;
}

static int discovery_key(uint8_t key[16])
{
    static const uint8_t seed[] = "yGAdlopoPVldABfn";
    return mbedtls_md5(seed, sizeof(seed) - 1, key) ? LAN_ERR_CRYPTO : LAN_OK;
}

static bool add_string(cJSON *root, const char *name, const char *value)
{
    return value && cJSON_AddStringToObject(root, name, value) != NULL;
}

int lan_discovery_build(const tuya_lan_config_t *config, uint8_t *out,
                        size_t capacity, size_t *len)
{
    cJSON *root;
    char *json = NULL;
    uint8_t key[16], *plaintext = NULL;
    size_t json_len;
    int result = LAN_ERR_MEMORY;
    if (len) *len = 0;
    if (!config || !config->pal || !config->pal->malloc || !config->pal->free ||
        !out || !len || !config->device_id || !config->uuid || !config->ipv4 ||
        !config->product_key) return LAN_ERR_PARAM;
    root = cJSON_CreateObject();
    if (!root) return LAN_ERR_MEMORY;
    if (!add_string(root, "ip", config->ipv4) || !add_string(root, "gwId", config->device_id) ||
        !add_string(root, "uuid", config->uuid) || !add_string(root, "productKey", config->product_key) ||
        !add_string(root, "version", "3.5") || !cJSON_AddNumberToObject(root, "active", 2) ||
        !cJSON_AddNumberToObject(root, "ablilty", 0) || !cJSON_AddBoolToObject(root, "encrypt", true) ||
        !cJSON_AddNumberToObject(root, "sl", 1)) goto done;
    json = cJSON_PrintUnformatted(root);
    if (!json) goto done;
    json_len = strlen(json);
    if (json_len > MAX_PLAINTEXT - RETURN_CODE) { result = LAN_ERR_LIMIT; goto done; }
    plaintext = config->pal->malloc(json_len + RETURN_CODE);
    if (!plaintext) goto done;
    put32(plaintext, 0);
    memcpy(plaintext + RETURN_CODE, json, json_len);
    result = discovery_key(key);
    if (!result) result = encrypt(config->pal, key, 0, CMD_DISCOVERY, plaintext,
                                  json_len + RETURN_CODE, out, capacity, len);
 done:
    mbedtls_platform_zeroize(key, sizeof(key));
    config->pal->free(plaintext); cJSON_free(json); cJSON_Delete(root);
    return result;
}

int lan_discovery_parse(const tuya_lan_config_t *config, const uint8_t *data,
                        size_t len, char *ipv4, size_t capacity)
{
    uint8_t key[16], *plaintext;
    size_t plain_len = 0, ip_len;
    cJSON *root = NULL, *ip, *from;
    int result;
    if (!config || !config->pal || !config->pal->malloc || !config->pal->free ||
        !data || !ipv4 || !capacity) return LAN_ERR_PARAM;
    if (len < FRAME_OVERHEAD || len > TUYA_LAN_MAX_FRAME_SIZE ||
        get32(data + 10) != CMD_DISCOVERY_QUERY) return LAN_ERR_PROTOCOL;
    plaintext = config->pal->malloc(len - FRAME_OVERHEAD + 1);
    if (!plaintext) return LAN_ERR_MEMORY;
    result = discovery_key(key);
    if (!result) result = decrypt(key, data, len, plaintext, &plain_len);
    if (result) goto done;
    root = parse_json(config, (const char *)plaintext, plain_len);
    ip = unique_member(root, "ip"); from = unique_member(root, "from");
    if (!cJSON_IsString(ip) || !ip->valuestring || !from) { result = LAN_ERR_PROTOCOL; goto done; }
    ip_len = strlen(ip->valuestring);
    if (!ip_len || ip_len >= capacity) { result = LAN_ERR_LIMIT; goto done; }
    memcpy(ipv4, ip->valuestring, ip_len + 1);
 done:
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(plaintext, len - FRAME_OVERHEAD + 1);
    config->pal->free(plaintext); cJSON_Delete(root);
    return result;
}
