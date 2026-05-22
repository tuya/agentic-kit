/**
 * @file scan_by_device_pair_demo.c
 * @brief Scan-by-device pairing demo: app shows QR, device scans it.
 *
 * Flow:
 *  1. stbi_load() -- decode JPEG to an 8-bit grayscale pixel buffer
 *  2. quirc       -- locate and decode the QR code in that buffer
 *  3. json_get_str() -- extract "s" (SSID), "p" (password), "t" (token)
 *     from the fixed-format payload {"s":"...","p":"...","t":"..."}
 *  4. iot_client_init_on_boarding_with_token() -- activate device with token
 *  5. iot_client_get_session_token() -- verify connectivity, then clean up
 */

#include "scan_by_device_pair_demo.h"
#include "iot_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "quirc.h"

#define TAG "scan_by_device"

/* -- JSON helper ----------------------------------------------------------- */

static int json_get_str(const char *json, const char *key,
                        char *out, size_t cap)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* -- Public entry point ---------------------------------------------------- */

int demo_scan_by_device_pair_run(const char *jpg_path,
                                 const char *uuid, const char *authkey,
                                 const char *product_key,
                                 const char *firmware_key)
{
    /* -- 1. Load JPEG as grayscale ----------------------------------------- */
    int w, h;
    uint8_t *pixels = stbi_load(jpg_path, &w, &h, NULL, 1);
    if (!pixels) {
        fprintf(stderr, "[%s] Failed to load image: %s\n", TAG, jpg_path);
        return -1;
    }
    printf("[%s] Image loaded: %s (%dx%d)\n", TAG, jpg_path, w, h);

    /* -- 2. Decode QR code ------------------------------------------------- */
    struct quirc *q = quirc_new();
    if (!q) {
        fprintf(stderr, "[%s] quirc_new failed\n", TAG);
        stbi_image_free(pixels);
        return -1;
    }

    if (quirc_resize(q, w, h) < 0) {
        fprintf(stderr, "[%s] quirc_resize failed\n", TAG);
        quirc_destroy(q);
        stbi_image_free(pixels);
        return -1;
    }

    uint8_t *buf = quirc_begin(q, NULL, NULL);
    memcpy(buf, pixels, (size_t)w * (size_t)h);
    quirc_end(q);
    stbi_image_free(pixels);

    if (quirc_count(q) < 1) {
        fprintf(stderr, "[%s] No QR code found in image\n", TAG);
        quirc_destroy(q);
        return -1;
    }

    struct quirc_code code;
    struct quirc_data data;
    quirc_extract(q, 0, &code);
    quirc_decode_error_t decode_err = quirc_decode(&code, &data);
    quirc_destroy(q);

    if (decode_err != QUIRC_SUCCESS) {
        fprintf(stderr, "[%s] QR decode error: %s\n", TAG,
                quirc_strerror(decode_err));
        return -1;
    }

    const char *payload = (const char *)data.payload;
    printf("[%s] QR payload: %s\n", TAG, payload);

    /* -- 3. Parse JSON fields ---------------------------------------------- */
    char ssid[64]    = {0};
    char password[64] = {0};
    char token[128]  = {0};

    if (json_get_str(payload, "s", ssid,     sizeof(ssid))     < 0 ||
        json_get_str(payload, "p", password, sizeof(password)) < 0 ||
        json_get_str(payload, "t", token,    sizeof(token))    < 0) {
        fprintf(stderr, "[%s] Failed to parse QR payload "
                        "(expected {\"s\":...,\"p\":...,\"t\":...})\n", TAG);
        return -1;
    }

    printf("[%s] SSID    : %s\n", TAG, ssid);
    printf("[%s] Password: %s\n", TAG, password);
    printf("[%s] Token   : %s\n", TAG, token);

    /* -- 4. Activate device via on-boarding token -------------------------- */
    iot_init_default();

    iot_on_boarding_config_t cfg = {
        .timeout_ms       = 10000,
        .env              = PROD,
        .mqtt_disable_tls = false,
    };
    memcpy((char *)cfg.uuid,         uuid,         strlen(uuid));
    memcpy((char *)cfg.authkey,      authkey,      strlen(authkey));
    memcpy((char *)cfg.product_key,  product_key,  strlen(product_key));
    memcpy((char *)cfg.firmware_key, firmware_key, strlen(firmware_key));

    printf("[%s] Starting on-boarding with token...\n", TAG);
    iot_client_t *client = iot_client_init_on_boarding_with_token(&cfg, token);
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init_on_boarding_with_token failed\n", TAG);
        return -1;
    }

    printf("[%s] Activation successful!\n", TAG);
    printf("[%s] devid      : %s\n", TAG, client->devid);
    printf("[%s] secret_key : %s\n", TAG, client->secret_key);
    printf("[%s] local_key  : %s\n", TAG, client->local_key);

    /* -- 5. Verify connectivity with a session token request --------------- */
    char session_token[2048] = {0};
    int ret = iot_client_get_session_token(client, NULL, session_token, sizeof(session_token));
    if (ret != 0 || session_token[0] == '\0') {
        fprintf(stderr, "[%s] iot_client_get_session_token failed: %d\n", TAG, ret);
        iot_client_deinit(client);
        return -1;
    }
    printf("[%s] Session token acquired (len=%zu)\n", TAG, strlen(session_token));

    iot_client_deinit(client);
    return 0;
}
