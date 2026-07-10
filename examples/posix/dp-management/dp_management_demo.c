/**
 * @file dp_management_demo.c
 * @brief End-to-end DP (Data Point) management demo for an activated device.
 *
 * This is the "device reboot" scenario the DP layer is designed for: the device
 * was activated earlier (see pair/api-activate), its credentials + schema +
 * last DP state were persisted, and now it boots, restores that state, connects,
 * and runs the steady-state DP loop.
 *
 * Lifecycle shown here:
 *   1. Read the schema (schema.json) and DP state (dp_state.json) from separate files.
 *   2. iot_client_init() with mqtt_auto_connect=false -> DP registry built from schema.
 *      Then validate the DP state against the schema and restore it only if it
 *      fully conforms (iot_dp_validate_json), discarding a stale/mismatched file.
 *   3. Fetch the MQTT CA via IoT DNS, then iot_client_message_connect().
 *   4. iot_dp_report_all() right after connect (the cloud only learns state from
 *      reports; "report on connect" is mandatory app behaviour — see the ADR).
 *   5. Loop: pump downlinks (iot_client_message_process), and on a dropped link
 *      reconnect + re-report. Periodically simulate a local change (iot_dp_set +
 *      iot_dp_report_all_dirty) and poll for a schema upgrade.
 *   6. Persist on every change via the save callback; persist a newer schema via
 *      the schema-update callback. The SDK provides the mechanism; the app owns
 *      the storage.
 *   7. On Ctrl-C: dump the final state, disconnect, free, deinit.
 */

#include "dp_management_demo.h"

#include "iot_client.h"
#include "iot_client_message.h"   /* manual connect/disconnect/process (app-owned loop) */
#include "iot_dp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>   /* sleep() */

#define TAG "dp_demo"

/* App-owned persistence files. The SDK never touches storage — these are written
 * by our callbacks and read back on the next boot. */
#define DP_STATE_PATH "dp_state.json"
#define SCHEMA_PATH   "schema.json"

/* Steady-state cadences (seconds). */
#define SENSOR_PERIOD_S       5
#define SCHEMA_POLL_PERIOD_S  60

/* Fallback product schema, used only when SCHEMA_PATH is absent (first boot).
 * This is a real Tuya schema (top-level "type":"obj"; the data type is under
 * property.type). Replace with YOUR product's schema — ids/types/modes must match
 * the product on the Tuya platform, otherwise the cloud rejects reports.
 *   1 bool  rw  - power switch
 *   2 value ro  - battery level 0..100 (%), device-reported
 *   3 value rw  - writable setpoint 0..100 (%)
 *   4 enum  ro  - charge status {none, charging, charge_done}, device-reported
 *   5 raw   ro  - opaque binary frame, device-reported (never persisted)
 * The schema "mode" (ro/rw/wr) is a cloud/app-side hint; the device SDK does not
 * enforce it — the device may set/report any DP, and the cloud may set any DP via
 * downlink. */
static const char *DEFAULT_SCHEMA =
    "["
    "{\"mode\":\"rw\",\"property\":{\"type\":\"bool\"},\"id\":1,\"type\":\"obj\"},"
    "{\"mode\":\"ro\",\"property\":{\"min\":0,\"max\":100,\"scale\":0,\"step\":1,\"type\":\"value\"},\"id\":2,\"type\":\"obj\"},"
    "{\"mode\":\"rw\",\"property\":{\"min\":0,\"max\":100,\"scale\":0,\"step\":1,\"type\":\"value\"},\"id\":3,\"type\":\"obj\"},"
    "{\"mode\":\"ro\",\"property\":{\"range\":[\"none\",\"charging\",\"charge_done\"],\"type\":\"enum\"},\"id\":4,\"type\":\"obj\"},"
    "{\"mode\":\"ro\",\"property\":{\"type\":\"raw\"},\"id\":5,\"type\":\"obj\"}"
    "]";

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ---- tiny file helpers (the app's "flash/NVS") --------------------------- */

/* Read a whole text file into a heap buffer (caller frees). NULL if absent. */
static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (buf) {
        if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
        else buf[len] = '\0';
    }
    fclose(f);
    return buf;
}

static void write_text_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[%s] cannot write %s\n", TAG, path); return; }
    fputs(text, f);
    fclose(f);
}

/* ---- value formatting (for readable logs) -------------------------------- */

static void format_dp_value(const iot_dp_value_t *v, char *out, size_t out_len)
{
    switch (v->type) {
    case IOT_DP_TYPE_BOOL:   snprintf(out, out_len, "bool=%s", v->value.boolean ? "true" : "false"); break;
    case IOT_DP_TYPE_VALUE:  snprintf(out, out_len, "value=%d", v->value.integer); break;
    case IOT_DP_TYPE_ENUM:   snprintf(out, out_len, "enum[%d]", v->value.enum_index); break;
    case IOT_DP_TYPE_STRING: snprintf(out, out_len, "string=\"%s\"", v->value.string ? v->value.string : ""); break;
    case IOT_DP_TYPE_RAW: {
        /* Preview the first few bytes as hex; raw is opaque, so there is no
         * canonical text form — show length + a hex head. */
        const uint8_t *d = v->value.raw.data;
        size_t len = v->value.raw.len;
        size_t shown = len < 8 ? len : 8;
        int off = snprintf(out, out_len, "raw[%zu]=", len);
        for (size_t i = 0; d && i < shown && off > 0 && (size_t)off < out_len - 3; i++)
            off += snprintf(out + off, out_len - (size_t)off, "%02X", d[i]);
        if (off > 0 && shown < len && (size_t)off < out_len - 3)
            snprintf(out + off, out_len - (size_t)off, "..");
        break;
    }
    default:                 snprintf(out, out_len, "?"); break;
    }
}

/* ---- callbacks ----------------------------------------------------------- */

/* Cloud -> device: a DP was set from the app/cloud. Fired once per changed DP.
 * The value (and any string/raw it points to) is valid only during this call. */
static void on_dp_downlink(uint8_t dp_id, const iot_dp_value_t *value, void *user_data)
{
    (void)user_data;
    char buf[96];
    format_dp_value(value, buf, sizeof(buf));
    printf("[%s] <- downlink DP %u: %s\n", TAG, dp_id, buf);
    /* Drive the hardware / UI from the new value here. We do NOT report it back:
     * the cloud already knows the value it just sent. */
}

/* DP state changed (local set, report, or downlink) -> persist the full snapshot.
 * Each callback carries the complete state, so debouncing flash writes is safe. */
static void on_dp_save(const char *dp_state_json, void *user_data)
{
    (void)user_data;
    printf("[%s] persist DP state -> %s\n", TAG, DP_STATE_PATH);
    write_text_file(DP_STATE_PATH, dp_state_json);
}

/* A newer schema was fetched for our schema_id -> persist it so the next boot
 * restores the upgraded definitions. */
static void on_schema_update(const char *schema_id, const char *new_schema, void *user_data)
{
    (void)user_data;
    printf("[%s] schema upgrade for id=%s -> %s\n", TAG, schema_id ? schema_id : "", SCHEMA_PATH);
    write_text_file(SCHEMA_PATH, new_schema);
}

/* ---- connection helpers -------------------------------------------------- */

/* Resolve and attach the MQTT broker's CA (when using TLS and none was supplied),
 * so the demo connects against the real cloud without bundling a cert file.
 * Returns the allocated cert (assign to client->cacert; free at shutdown) or NULL. */
static char *ensure_mqtt_ca(iot_client_t *client)
{
    if (client->mqtt_disable_tls || client->cacert || client->mqtt_url[0] == '\0')
        return NULL;

    char scheme[8] = {0};
    char host[128] = {0};
    unsigned port = 0;
    if (sscanf(client->mqtt_url, "%7[^:]://%127[^:]:%u", scheme, host, &port) != 3) {
        fprintf(stderr, "[%s] cannot parse mqtt_url: %s\n", TAG, client->mqtt_url);
        return NULL;
    }

    char *ca = NULL;
    if (iot_get_ca_certificate(client, host, (uint16_t)port, &ca) != OPRT_OK || !ca) {
        fprintf(stderr, "[%s] failed to fetch MQTT CA for %s:%u\n", TAG, host, port);
        return NULL;
    }
    client->cacert = ca;   /* must outlive the client */
    return ca;
}

/* Connect and immediately re-publish full state — the cloud only learns DP state
 * from device-initiated reports, so this runs after every (re)connect. */
static int connect_and_report(iot_client_t *client)
{
    int ret = iot_client_message_connect(client);
    if (ret != OPRT_OK) {
        fprintf(stderr, "[%s] MQTT connect failed: %d\n", TAG, ret);
        return ret;
    }
    printf("[%s] MQTT connected; reporting full state\n", TAG);
    iot_dp_report_all(client);
    return OPRT_OK;
}

/* ---- the simulated device's own state changes ---------------------------- */

/* Pretend the device's own inputs changed: climb the battery level, derive the
 * charge status, occasionally toggle power, and update the writable setpoint. Set
 * locally (marks dirty), then batch-report the dirty DPs in one uplink message.
 *
 * DP2 (battery) and DP4 (charge status) are `ro` — report-only on the cloud side,
 * but the device may still set/report them (the SDK does not enforce "mode");
 * downlink may set any DP. */
static void simulate_local_change(iot_client_t *client, int tick)
{
    /* Battery level climbs 0..100 then wraps. */
    int battery = (tick * 5) % 101;
    iot_dp_value_t level = { .type = IOT_DP_TYPE_VALUE, .value.integer = battery };
    iot_dp_set(client, 2, &level);

    /* Charge status enum: cycle through the schema range[] (0=none, 1=charging,
     * 2=charge_done) so every value is exercised and visibly changes. */
    int status = tick % 3;
    iot_dp_value_t charge = { .type = IOT_DP_TYPE_ENUM, .value.enum_index = status };
    iot_dp_set(client, 4, &charge);

    /* Power switch (rw) toggles every few ticks. */
    if (tick % 3 == 0) {
        iot_dp_value_t power = { .type = IOT_DP_TYPE_BOOL, .value.boolean = (tick % 6 == 0) };
        iot_dp_set(client, 1, &power);
    }

    /* A writable numeric DP (rw); the cloud can also set it via downlink. */
    iot_dp_value_t setpoint = { .type = IOT_DP_TYPE_VALUE, .value.integer = (tick * 10) % 101 };
    iot_dp_set(client, 3, &setpoint);

    /* A raw DP (ro = report-only): pack a small opaque frame (magic + LE tick).
     * The SDK copies the buffer on set, so this stack buffer is safe to drop.
     * Reported uplink each tick but never persisted — it won't appear in
     * dp_state.json. */
    uint8_t frame[4] = { 0xDE, 0xAD, (uint8_t)tick, (uint8_t)(tick >> 8) };
    iot_dp_value_t frame_v = { .type = IOT_DP_TYPE_RAW, .value.raw = { frame, sizeof(frame) } };
    iot_dp_set(client, 5, &frame_v);

    int ret = iot_dp_report_all_dirty(client);
    printf("[%s] -> reported (tick %d, battery %d%%, status %d), rc=%d\n",
           TAG, tick, battery, status, ret);
}

/* Show the current cached value of a DP (read-back via iot_dp_get).
 *
 * For STRING/RAW DPs iot_dp_get returns a pointer into the live DP cache that the
 * next write to the same dp_id frees. It is safe here because we consume it
 * immediately — format_dp_value copies it into buf, on this one thread, before any
 * later set/report. Do NOT stash the returned pointer for later use, and in a
 * multi-threaded design serialize this read against writes to the same dp_id. */
static void log_current_dp(iot_client_t *client, uint8_t dp_id)
{
    iot_dp_value_t v;
    if (iot_dp_get(client, dp_id, &v) == OPRT_OK) {
        char buf[96];
        format_dp_value(&v, buf, sizeof(buf));
        printf("[%s]    DP %u currently %s\n", TAG, dp_id, buf);
    }
}

/* ---- entry point --------------------------------------------------------- */

int demo_dp_management_run(const char *devid,
                           const char *secret_key,
                           const char *local_key,
                           const char *schema_id)
{
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "[%s] iot_init_default failed\n", TAG);
        return -1;
    }
    log_set_level(LOG_INFO);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 1. Read the persisted schema and DP state from separate files (both NULL on
     * first boot). The DP state is validated against the schema below, AFTER the
     * registry is built — not blindly fed into config. */
    char *saved_schema = read_text_file(SCHEMA_PATH);
    char *saved_state  = read_text_file(DP_STATE_PATH);
    if (saved_schema) printf("[%s] loaded schema from %s\n", TAG, SCHEMA_PATH);
    if (saved_state)  printf("[%s] loaded DP state from %s: %s\n", TAG, DP_STATE_PATH, saved_state);

    iot_client_config_t cfg = {
        .region           = AY,      /* match your device's region/env */
        .env              = PROD,
        .mqtt_disable_tls = false,   /* mqtts */
        .mqtt_auto_connect = false,  /* we own the connect/reconnect loop */
        .schema           = saved_schema ? saved_schema : DEFAULT_SCHEMA,
        .schema_id        = schema_id,
        .dp_state         = NULL,    /* don't auto-restore — we validate first (step 2b) */
    };
    strncpy(cfg.devid,      devid,      sizeof(cfg.devid) - 1);
    strncpy(cfg.secret_key, secret_key, sizeof(cfg.secret_key) - 1);
    strncpy(cfg.local_key,  local_key,  sizeof(cfg.local_key) - 1);

    /* 2. Init: builds the DP registry from the schema. */
    iot_client_t *client = iot_client_init(&cfg);
    free(saved_schema);  /* init copied the schema */
    if (!client) {
        fprintf(stderr, "[%s] iot_client_init failed\n", TAG);
        free(saved_state);
        return -1;
    }
    printf("[%s] client up: devid=%s schema_id=%s\n", TAG, client->devid, client->schema_id);

    /* 2b. Validate the persisted DP state against the schema, all-or-nothing:
     * restore only if every entry conforms; otherwise discard it (a stale file
     * from an older schema, hand-edited junk, etc. is dropped rather than
     * partially applied). The next save will overwrite it with valid state. */
    if (saved_state) {
        int vrc = iot_dp_validate_json(client, saved_state);
        if (vrc == OPRT_OK) {
            iot_dp_restore_json(client, saved_state);
            printf("[%s] DP state matches schema — restored\n", TAG);
        } else {
            printf("[%s] DP state does not match schema (rc=%d) — discarded\n", TAG, vrc);
        }
        free(saved_state);
    }

    /* 3. Register the three DP callbacks. */
    iot_dp_set_callback(client, on_dp_downlink, NULL);
    iot_dp_set_save_callback(client, on_dp_save, NULL);
    iot_dp_set_schema_update_callback(client, on_schema_update, NULL);

    /* 4. CA + connect + report-on-connect. */
    char *mqtt_ca = ensure_mqtt_ca(client);
    if (connect_and_report(client) != OPRT_OK) {
        if (mqtt_ca) client->pal->free(mqtt_ca);
        iot_client_deinit(client);
        return -1;
    }

    /* 5. Steady-state loop. */
    printf("[%s] entering DP loop (Ctrl-C to stop)\n", TAG);
    int sensor_tick = 0;
    time_t last_sensor = 0;
    time_t last_schema = time(NULL);   /* don't poll schema immediately */
    while (g_running) {
        /* Pump the receive path; downlinks dispatch into on_dp_downlink. */
        int rc = iot_client_message_process(client, 200);
        if (rc != OPRT_OK) {
            /* No auto-reconnect in the SDK: the app reconnects, then re-reports. */
            fprintf(stderr, "[%s] link error %d; reconnecting...\n", TAG, rc);
            iot_client_message_disconnect(client);
            if (connect_and_report(client) != OPRT_OK) {
                sleep(2);
                continue;
            }
        }

        time_t now = time(NULL);
        if (now - last_sensor >= SENSOR_PERIOD_S) {
            last_sensor = now;
            simulate_local_change(client, ++sensor_tick);
            log_current_dp(client, 1);
            log_current_dp(client, 2);
            log_current_dp(client, 3);
            log_current_dp(client, 4);
            log_current_dp(client, 5);
        }
        if (now - last_schema >= SCHEMA_POLL_PERIOD_S) {
            last_schema = now;
            printf("[%s] polling for schema upgrade...\n", TAG);
            iot_dp_schema_check_update(client);  /* fires on_schema_update if newer */
        }
    }

    /* 6. Pull the final state on demand (alternative to the save callback). */
    printf("\n[%s] shutting down\n", TAG);
    char *final_state = NULL;
    if (iot_dp_dump_json(client, &final_state) == OPRT_OK && final_state) {
        printf("[%s] final DP state: %s\n", TAG, final_state);
        write_text_file(DP_STATE_PATH, final_state);
        client->pal->free(final_state);
    }

    /* 7. Tear down. */
    iot_client_message_disconnect(client);
    if (mqtt_ca) client->pal->free(mqtt_ca);
    iot_client_deinit(client);
    return 0;
}
