/*
 * agent_trigger_demo.c -- Agent-trigger (智能体触发器) end-to-end demo.
 *
 * A cloud agent trigger has three parts, and only the last one is code:
 *
 *   1. a DEVICE EVENT RULE on the product -- a DP condition such as
 *      "dp2 < 20" -- configured on the Tuya platform;
 *   2. an AGENT TRIGGER bound to that event, whose task is "agent pushes a
 *      message" and whose Prompt may interpolate DP values ({{dp2}});
 *   3. THIS demo: the device that makes the rule match, and that holds an AI
 *      session open for the pushed message to land on.
 *
 * So the demo runs two halves at once, one per SDK module:
 *
 *   uplink   (iot-client / MQTT)   report the DPs that satisfy the rule
 *   downlink (rtc-tcp-client/TAI)  an idle session that receives the push
 *
 * The demo NEVER calls tai_send_text(). Every turn it receives is therefore one
 * the server started on its own -- which is exactly what a trigger produces. A
 * pushed turn has the same shape as a reply to a question:
 *
 *   EVT_START -> NLG text chunks -> TTS audio frames -> EVT_END
 *
 * Order matters: the session is opened BEFORE the DP report. A trigger that
 * fires while the device holds no session has nowhere to push to.
 *
 * The rule is evaluated on a TRANSITION, so the demo reports three values 2 s
 * apart: the baseline (dp2 = 99), then a random value inside the DP's
 * range, then the one that fires the rule (dp2 = 5). Re-reporting a
 * value the cloud already holds is not a transition and may not fire anything,
 * which is what the random middle step defends against -- it guarantees a real
 * change even if an earlier run left the cloud on either endpoint. Pin it with
 * --mid N, drop it with --no-mid, drop the baseline with --no-baseline.
 *
 * Build (from examples/posix):
 *   cmake -S . -B build
 *   cmake --build build --target tai_agent_trigger_demo
 *
 * Usage:
 *   ./build/tai_agent_trigger_demo --help
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "cJSON.h"

#include "tuya_ai.h"
#include "iot_client.h"
#include "iot_dp.h"

#include "demo_json.h"
#include "demo_mcp.h"
#include "demo_reconnect.h"
#include "demo_text.h"

extern const pal_t *tai_pal_posix(void);

/* -- Defaults ------------------------------------------------------------- */

/* The device is baked in on purpose: a trigger demo is only meaningful against
 * the product (PID) whose event rule and agent trigger were configured for it,
 * and that product is what pins the schema below. Pointing this demo at some
 * other device would need that device's schema and its own cloud-side rule, so
 * there is nothing useful to parameterise -- edit these five values instead. */
#define DEFAULT_DEVID      "6cd370251e8be96de8vwoe"
#define DEFAULT_SECRET_KEY "[SPT;N:b@)wPzK/)"
#define DEFAULT_LOCAL_KEY  "#d[<4y*N.vE]RAAG"
#define DEFAULT_REGION     AY      /* China */
#define DEFAULT_ENV        PROD

#define DEFAULT_BATTERY_DP   2
/* Used as-is for a value DP; for a bool DP they are overridden to 1 / 0 unless
 * given on the command line -- see main(). */
#define DEFAULT_BATTERY      5     /* the state that fires the rule          */
#define DEFAULT_BASELINE     99    /* the state reported first               */
#define DEFAULT_TIMEOUT_S    120
#define DEFAULT_AUDIO_PATH   "output_trigger_tts.pcm"

/* Seconds between consecutive DP reports. The loop below polls the MQTT socket
 * in 200 ms slices, so this is a real wait during which the client stays
 * responsive -- long enough for the cloud to record each value as a distinct
 * state rather than coalescing the three into one. */
#define REPORT_INTERVAL_S    2

/* Consecutive broker failures before the run is abandoned. */
#define MQTT_MAX_CONSECUTIVE_FAILS  8

/* The product's schema, from the DP snapshot the cloud returned at activation:
 *   {"dps":{"1":true,"2":0}}
 *
 *   1  bool
 *   2  value   <- rule input (DEFAULT_BATTERY_DP)
 *
 * Both DPs are carried, not just the rule input: the registry has to match the
 * DPs the device actually has, or a downlink for the other one comes back
 * OPRT_DP_INVALID_ID. No enum DP exists, so the rule is a single condition on
 * the battery DP. Any raw DPs are absent here because raw is never in a DP
 * snapshot. Pointing the demo at another product is an edit to this literal and
 * to DEFAULT_BATTERY_DP, in the same block as the credentials above: the device
 * fixes the product, so there is nothing worth passing at run time.
 *
 * dp2 serialises as a bare number, which means value (an enum with a range
 * would come back as its label string). Inferred rather than read from the real
 * schema, so correct against the platform if it matters: dp2's min/max
 * (0..100 assumed, the shape of a percentage -- outside the real range
 * iot_dp_set fails locally with OPRT_DP_VALUE_OUT_OF_RANGE, which at least says
 * so), and both `mode`s, which the SDK never parses or enforces anyway. */
static const char *DEFAULT_SCHEMA =
    "["
    "{\"mode\":\"rw\",\"property\":{\"type\":\"bool\"},\"id\":1,\"type\":\"obj\"},"
    "{\"mode\":\"ro\",\"property\":{\"min\":0,\"max\":100,\"scale\":0,\"step\":1,\"type\":\"value\"},\"id\":2,\"type\":\"obj\"}"
    "]";

/* -- Options -------------------------------------------------------------- */

typedef struct {
    const char *agent_code;      /* NULL = the product's default agent        */
    const char *audio_path;      /* "" disables writing TTS audio             */
    iot_dp_type_t trigger_type; /* resolved from the schema, not a CLI option  */
    int         battery_set;    /* --battery given: don't re-default for bool  */
    int         baseline_set;
    long        mid;            /* value reported between baseline and trigger */
    int         mid_set;        /* --mid given: use it instead of a random one  */
    int         use_mid;
    long        battery;         /* value that satisfies the rule             */
    long        baseline;        /* healthy value reported first              */
    int         use_baseline;
    int         listen_only;     /* report nothing; just wait for a push      */
    int         timeout_s;
    unsigned    repeat;          /* stop after N pushes; 0 = never stop       */
    int         verbose;
} opts_t;

/* Names for the banner only. Worth printing: if DEFAULT_REGION ever stops
 * matching the data center the device belongs to, the SDK does not say so --
 * IoT-DNS answers 200 with no endpoint, mqtt_url comes back empty, and the
 * client sits there having "succeeded" while connected to nothing. */
static const struct { const char *name; iot_region_t region; } REGION_NAMES[] = {
    { "AY",   AY   }, { "AZ",   AZ   }, { "UE", UEAZ }, { "UEAZ", UEAZ },
    { "EU",   EU   }, { "WE", WEAZ }, { "WEAZ", WEAZ }, { "IN",   IN   },
    { "SG",   SG   },
};

static const struct { const char *name; iot_env_t env; } ENV_NAMES[] = {
    { "prod", PROD }, { "pre", PRE }, { "test", TEST },
};

static const char *region_name(iot_region_t r)
{
    for (size_t i = 0; i < sizeof(REGION_NAMES) / sizeof(REGION_NAMES[0]); i++)
        if (REGION_NAMES[i].region == r) return REGION_NAMES[i].name;
    return "?";
}

static const char *env_name(iot_env_t e)
{
    for (size_t i = 0; i < sizeof(ENV_NAMES) / sizeof(ENV_NAMES[0]); i++)
        if (ENV_NAMES[i].env == e) return ENV_NAMES[i].name;
    return "?";
}

static void usage(const char *argv0)
{
    printf(
"Usage: %s [options]\n"
"\n"
"Reports the DPs that make a cloud device-event rule match, then prints the\n"
"message the agent trigger pushes back over an idle AI session.\n"
"\n"
"The device, its product schema and the trigger DP are all compiled in\n"
"(DEFAULT_* at the top of this file): the demo only means anything against the\n"
"product whose cloud-side rule and trigger were set up for it, so pointing it\n"
"elsewhere is an edit there, not a flag.\n"
"Device: %s (%s / %s), battery DP %d.\n"
"\n"
"What to report:\n"
"      --battery N           battery value that fires the rule   (default: %ld)\n"
"      --baseline N          healthy battery reported first      (default: %ld)\n"
"      --mid N               value reported between them  (default: random in range)\n"
"      --no-baseline         skip the baseline report (no transition is created)\n"
"      --no-mid              skip the middle report\n"
"      --listen              report nothing; only wait for a push\n"
"\n"
"Waiting:\n"
"      --timeout S           seconds to wait for a push          (default: %d)\n"
"      --repeat N            exit after N pushes, 0 = never      (default: 1)\n"
"      --audio FILE          write pushed TTS audio here         (default: %s)\n"
"                            pass an empty string to discard it\n"
"\n"
"Other:\n"
"  -a, --agent-code CODE     agent code   (default: product's default agent)\n"
"  -v, --verbose             enable SDK debug logging\n"
"  -h, --help                this text\n",
        argv0, DEFAULT_DEVID, region_name(DEFAULT_REGION), env_name(DEFAULT_ENV),
        DEFAULT_BATTERY_DP,
        (long)DEFAULT_BATTERY, (long)DEFAULT_BASELINE,
        DEFAULT_TIMEOUT_S, DEFAULT_AUDIO_PATH);
}

/* Read the value that follows --flag, or complain and return NULL. */
static const char *next_arg(int argc, char **argv, int *i, const char *flag)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", flag);
        return NULL;
    }
    return argv[++(*i)];
}

/* Parse a decimal integer in [lo, hi]; -1 on anything else (including trailing
 * junk, which would otherwise silently accept "20abc" as 20). */
static int parse_long(const char *s, long lo, long hi, long *out, const char *what)
{
    char *end = NULL;
    long  v   = strtol(s, &end, 10);
    if (end == s || (end && *end != '\0') || v < lo || v > hi) {
        fprintf(stderr, "%s: expected an integer in [%ld, %ld], got \"%s\"\n",
                what, lo, hi, s);
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_opts(int argc, char **argv, opts_t *o)
{
    memset(o, 0, sizeof(*o));
    o->audio_path      = DEFAULT_AUDIO_PATH;
    o->use_mid         = 1;
    o->battery         = DEFAULT_BATTERY;
    o->baseline        = DEFAULT_BASELINE;
    o->use_baseline    = 1;
    o->timeout_s       = DEFAULT_TIMEOUT_S;
    o->repeat          = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = NULL;
        long        n = 0;

#define TAKE(flag) ((v = next_arg(argc, argv, &i, flag)) == NULL ? -1 : 0)

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 1; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) o->verbose = 1;
        else if (!strcmp(a, "--listen"))      o->listen_only  = 1;
        else if (!strcmp(a, "--no-baseline")) o->use_baseline = 0;
        else if (!strcmp(a, "--no-mid"))      o->use_mid      = 0;
        else if (!strcmp(a, "--mid")) {
            o->mid_set = 1;
            if (TAKE(a) || parse_long(v, -2147483647L, 2147483647L, &o->mid, a)) return -1;
        }
        else if (!strcmp(a, "-a") || !strcmp(a, "--agent-code")) {
            if (TAKE(a)) return -1; o->agent_code = v;
        } else if (!strcmp(a, "--audio")) {
            if (TAKE(a)) return -1; o->audio_path = v;
        } else if (!strcmp(a, "--battery")) {
            o->battery_set = 1;
            if (TAKE(a) || parse_long(v, -2147483647L, 2147483647L, &o->battery, a)) return -1;
        } else if (!strcmp(a, "--baseline")) {
            o->baseline_set = 1;
            if (TAKE(a) || parse_long(v, -2147483647L, 2147483647L, &o->baseline, a)) return -1;
        } else if (!strcmp(a, "--timeout")) {
            if (TAKE(a) || parse_long(v, 1, 86400, &n, a)) return -1;
            o->timeout_s = (int)n;
        } else if (!strcmp(a, "--repeat")) {
            if (TAKE(a) || parse_long(v, 0, 1000000, &n, a)) return -1;
            o->repeat = (unsigned)n;
        } else {
            fprintf(stderr, "unknown option: %s\n\n", a);
            usage(argv[0]);
            return -1;
        }
#undef TAKE
    }
    return 0;
}

/* -- Demo context ---------------------------------------------------------- */

/* Written by the SDK worker thread (the TAI receive callbacks), read by the main
 * thread. The `volatile` fields are the ones main() polls; everything else is
 * touched only from the worker, which is single-threaded, so no lock is needed.
 *
 * fire_us is the exception in the other direction: main() writes it once, before
 * the report that can produce a push, and the worker only reads it afterwards. */
typedef struct {
    volatile int64_t  fire_us;        /* when the trigger DPs were reported   */
    volatile unsigned pushes;         /* completed pushed turns               */
    volatile int      timeover;       /* server said the session idled out    */

    int      turn_active;
    int      saw_payload;             /* text or audio seen in this turn      */
    char     event_id[64];
    int64_t  turn_start_us;
    int64_t  first_text_us;
    int64_t  first_audio_us;

    FILE    *audio_fp;
    size_t   turn_audio_bytes;
    size_t   total_audio_bytes;
    uint8_t  audio_codec;
    uint32_t audio_sample_rate;

    demo_textbuf_t   text;
    int              stream_printed;  /* this stream already printed NLG prose */
    demo_reconnect_t reconn;
} demo_ctx_t;

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static int64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

static double secs_since(int64_t from_us, int64_t to_us)
{
    if (from_us <= 0 || to_us <= 0) return -1.0;
    return (double)(to_us - from_us) / 1000000.0;
}

/* -- Schema helpers ------------------------------------------------------- */

/* Look up a DP's declared type in the schema. The trigger DP is reported with
 * whatever type this returns: iot_dp_set() rejects a mismatch outright
 * (OPRT_DP_TYPE_MISMATCH), so hardcoding one type here would break the demo
 * every time it is pointed at a DP of another kind. */
static int schema_dp_type(const char *schema_json, int dp_id, iot_dp_type_t *out,
                          long *out_min, long *out_max)
{
    static const struct { const char *name; iot_dp_type_t type; } TYPES[] = {
        { "bool",   IOT_DP_TYPE_BOOL   }, { "value", IOT_DP_TYPE_VALUE },
        { "string", IOT_DP_TYPE_STRING }, { "enum",  IOT_DP_TYPE_ENUM  },
        { "raw",    IOT_DP_TYPE_RAW    },
    };
    cJSON *root = cJSON_Parse(schema_json);
    if (!cJSON_IsArray(root)) {
        fprintf(stderr, "schema is not a JSON array\n");
        cJSON_Delete(root);
        return -1;
    }

    int         rc  = -1;
    const char *str = NULL;
    cJSON      *item = NULL;
    if (out_min) *out_min = 0;
    if (out_max) *out_max = 100;
    cJSON_ArrayForEach(item, root) {
        cJSON *jid = cJSON_GetObjectItem(item, "id");
        if (!cJSON_IsNumber(jid) || jid->valueint != dp_id) continue;
        /* Same precedence the SDK's parser uses: property.type first, then the
         * top-level type as the simplified form's fallback. */
        cJSON *prop  = cJSON_GetObjectItem(item, "property");
        cJSON *jptyp = cJSON_IsObject(prop) ? cJSON_GetObjectItem(prop, "type") : NULL;
        cJSON *jtyp  = cJSON_GetObjectItem(item, "type");
        str = cJSON_IsString(jptyp) ? jptyp->valuestring
            : (cJSON_IsString(jtyp) ? jtyp->valuestring : NULL);
        /* min/max live under property, same as the SDK reads them; the 0..100
         * fallback above only matters for a schema that omits them, where the
         * SDK leaves the bounds wide open anyway. */
        if (cJSON_IsObject(prop)) {
            cJSON *jmin = cJSON_GetObjectItem(prop, "min");
            cJSON *jmax = cJSON_GetObjectItem(prop, "max");
            if (out_min && cJSON_IsNumber(jmin)) *out_min = (long)jmin->valuedouble;
            if (out_max && cJSON_IsNumber(jmax)) *out_max = (long)jmax->valuedouble;
        }
        break;
    }
    if (!str) {
        fprintf(stderr, "DP %d is not in this schema (or has no type)\n", dp_id);
        goto out;
    }
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
        if (!strcmp(str, TYPES[i].name)) { *out = TYPES[i].type; rc = 0; goto out; }
    }
    fprintf(stderr, "DP %d has unsupported type \"%s\"\n", dp_id, str);
out:
    cJSON_Delete(root);
    return rc;
}

/* -- TAI callbacks (SDK worker thread) ------------------------------------ */

/* A pushed turn begins at whichever of EVT_START / text / audio arrives first.
 * The event boundary is not guaranteed to lead: a one-shot push can open with
 * its text, and treating that as "no turn in progress" would lose the turn. */
static void turn_begin(demo_ctx_t *dc, const char *event_id)
{
    if (dc->turn_active) return;

    dc->turn_active      = 1;
    dc->saw_payload      = 0;
    dc->turn_start_us    = now_us();
    dc->first_text_us    = 0;
    dc->first_audio_us   = 0;
    dc->turn_audio_bytes = 0;
    dc->stream_printed   = 0;   /* a MIDDLE-first stream must not inherit this */
    snprintf(dc->event_id, sizeof(dc->event_id), "%s", event_id ? event_id : "");

    printf("\n[push] server-initiated turn started");
    if (dc->event_id[0]) printf(" (event_id=%s)", dc->event_id);
    printf("\n");
    fflush(stdout);
}

static void turn_end(demo_ctx_t *dc)
{
    if (!dc->turn_active) return;
    dc->turn_active = 0;

    int64_t end_us = now_us();

    if (!dc->saw_payload) {
        /* START immediately followed by END: the server opened and closed a turn
         * without saying anything. Not a pushed message, so it must not satisfy
         * --repeat -- otherwise the demo exits reporting a push it never got. */
        printf("[push] turn ended with no text and no audio -- ignoring\n");
        fflush(stdout);
        return;
    }

    printf("[push] turn complete: %.2f s", secs_since(dc->turn_start_us, end_us));
    if (dc->fire_us > 0) {
        printf(", trigger->first text %.2f s", secs_since(dc->fire_us, dc->first_text_us));
        if (dc->first_audio_us > 0)
            printf(", trigger->first audio %.2f s",
                   secs_since(dc->fire_us, dc->first_audio_us));
    }
    if (dc->turn_audio_bytes > 0) {
        printf(", tts %zu bytes", dc->turn_audio_bytes);
        /* Only PCM has a byte->duration relation this simple (16-bit mono);
         * Opus frame sizes vary, so no duration is claimed for it. */
        if (dc->audio_codec == TAI_AUDIO_PCM && dc->audio_sample_rate > 0)
            printf(" (%.2f s @%u Hz PCM)",
                   (double)dc->turn_audio_bytes / (double)(dc->audio_sample_rate * 2),
                   dc->audio_sample_rate);
        else if (dc->audio_codec == TAI_AUDIO_OPUS)
            printf(" (Opus)");
    }
    printf("\n");
    fflush(stdout);

    dc->pushes++;   /* published last: main() gates the loop on this */
}

/* One reassembled text stream. NLG prose has already been streamed chunk by
 * chunk (the pushed message itself, printed on its own line); anything else -- a
 * SKILL payload, a structured workflow output -- is dumped once as a whole
 * document rather than as fragments. */
static void handle_complete_text(demo_ctx_t *dc)
{
    if (dc->stream_printed) {
        printf("\n");            /* close the streamed prose line */
        fflush(stdout);
        return;
    }
    printf("[push] non-NLG payload: ");
    fwrite(dc->text.buf, 1, dc->text.len, stdout);
    printf("\n");
    fflush(stdout);
}

static void on_text(tai_ctx_t *ctx, const tai_text_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    turn_begin(dc, msg->event_id);
    if (!dc->first_text_us) dc->first_text_us = now_us();
    dc->saw_payload = 1;

    if (msg->stream_flag == TAI_STREAM_START ||
        msg->stream_flag == TAI_STREAM_ONE_SHOT)
        dc->stream_printed = 0;

    /* NLG prose is one self-contained JSON line per chunk: print as it arrives,
     * bounded by msg->len -- the slice carries no NUL terminator. */
    if (nlg_print_content(msg->text, msg->len))
        dc->stream_printed = 1;   /* even an empty terminator line counts */

    /* In parallel, reassemble: a structured payload is one JSON document that can
     * straddle chunk boundaries, so it is only read once the stream ends. */
    if (demo_textbuf_accum(&dc->text, msg) == 1)
        handle_complete_text(dc);
}

static void on_audio(tai_ctx_t *ctx, const tai_audio_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    turn_begin(dc, msg->event_id);
    if (!dc->first_audio_us) dc->first_audio_us = now_us();
    dc->saw_payload = 1;

    if (msg->codec)       dc->audio_codec       = msg->codec;
    if (msg->sample_rate) dc->audio_sample_rate = msg->sample_rate;

    if (dc->audio_fp && msg->data && msg->len > 0) {
        size_t w = fwrite(msg->data, 1, msg->len, dc->audio_fp);
        dc->turn_audio_bytes  += w;
        dc->total_audio_bytes += w;
        if (w != msg->len)
            fprintf(stderr, "[push] short write to the audio file (%zu of %zu)\n",
                    w, msg->len);
    }
}

static void on_event(tai_ctx_t *ctx, const tai_event_msg_t *msg, void *ud)
{
    demo_ctx_t *dc = (demo_ctx_t *)ud;

    switch (msg->event_type) {
    case TAI_EVT_START:
        turn_begin(dc, msg->event_id);
        break;

    case TAI_EVT_END:
        /* The SDK drops empty text frames, so a stream ended by a bare
         * zero-length END never completes in on_text. Backstop. */
        if (demo_textbuf_flush(&dc->text)) handle_complete_text(dc);
        turn_end(dc);
        break;

    case TAI_EVT_MCP_CMD:
        /* This demo exposes no tools, but the SDK's default session attributes
         * declare deviceMcp support, so the server is still owed a well-formed
         * answer. A trigger Prompt that asks the agent to act on the device
         * arrives this way. See demo_mcp.h, and mcp_demo.c for real tools. */
        demo_mcp_reply_no_tools(ctx, msg);
        break;

    case TAI_EVT_SERVER_TIMEOVER:
        /* An idle session is not kept forever. A device that exists to receive
         * pushes must therefore rebuild the session, not assume one connect
         * lasts all day -- main() does that via the reconnect path. */
        fprintf(stderr, "[tai] server reports the session timed out\n");
        dc->timeover = 1;
        break;

    case TAI_EVT_PAYLOADS_END:
        break;                  /* payload stream done; EVT_END closes the turn */

    default:
        /* CHAT_BREAK / SERVER_VAD / UPDATE_CONTEXT and anything the SDK tolerates
         * for forward compatibility: named, not swallowed, so an unexpected
         * server behaviour is visible while debugging a trigger. */
        fprintf(stderr, "[tai] event %u\n", (unsigned)msg->event_type);
        break;
    }
}

static void on_disconnect(tai_ctx_t *ctx, const tai_disconnect_msg_t *msg, void *ud)
{
    (void)ctx;
    demo_ctx_t *dc = (demo_ctx_t *)ud;
    fprintf(stderr, "\n[tai] disconnected: reason=%u close_code=%u detail=%u\n",
            (unsigned)msg->reason, (unsigned)msg->close_code, (unsigned)msg->detail);
    /* Runs on the worker thread: only flag it -- the main loop reconnects. */
    demo_reconnect_signal(&dc->reconn, msg->reason, msg->close_code);
}

/* -- IoT callbacks -------------------------------------------------------- */

/* Cloud -> device DP set. A trigger whose Prompt tells the agent to act on the
 * device (turn something off, change a mode) shows up here or as an MCP call. */
static void on_dp_downlink(uint8_t dp_id, const iot_dp_value_t *value, void *user_data)
{
    (void)user_data;
    switch (value->type) {
    case IOT_DP_TYPE_BOOL:
        printf("[dp] <- DP %u = %s\n", dp_id, value->value.boolean ? "true" : "false");
        break;
    case IOT_DP_TYPE_VALUE:
        printf("[dp] <- DP %u = %d\n", dp_id, value->value.integer);
        break;
    case IOT_DP_TYPE_ENUM:
        printf("[dp] <- DP %u = enum[%d]\n", dp_id, value->value.enum_index);
        break;
    case IOT_DP_TYPE_STRING:
        printf("[dp] <- DP %u = \"%s\"\n", dp_id,
               value->value.string ? value->value.string : "");
        break;
    default:
        printf("[dp] <- DP %u (raw/%d)\n", dp_id, (int)value->type);
        break;
    }
    fflush(stdout);
}

/* Anything on the device's MQTT inbound topic the DP layer did not consume.
 * Logged rather than ignored: it is where a cloud-side notice other than a DP
 * set would appear, and a silent drop makes such a message invisible. */
static void on_mqtt_message(const char *topic, size_t topic_len,
                            const uint8_t *data, size_t data_len)
{
    printf("[mqtt] <- %.*s: %.*s\n",
           (int)topic_len, topic, (int)data_len, (const char *)data);
    fflush(stdout);
}

/* -- Connection helpers --------------------------------------------------- */

/* Resolve and attach the MQTT broker's CA via IoT DNS, so the demo connects
 * against the real cloud without bundling a cert file. The cert lives in a
 * static buffer that outlives the client. */
static void ensure_mqtt_ca(iot_client_t *client)
{
    static char mqtt_ca[4096];

    if (client->mqtt_disable_tls || client->cacert || client->mqtt_url[0] == '\0')
        return;

    char     scheme[8] = {0};
    char     host[128] = {0};
    unsigned port      = 0;
    if (sscanf(client->mqtt_url, "%7[^:]://%127[^:]:%u", scheme, host, &port) != 3) {
        fprintf(stderr, "[iot] cannot parse mqtt_url: %s\n", client->mqtt_url);
        return;
    }
    if (iot_get_ca_certificate(client, host, (uint16_t)port,
                               mqtt_ca, sizeof(mqtt_ca)) != OPRT_OK) {
        fprintf(stderr, "[iot] failed to fetch the MQTT CA for %s:%u\n", host, port);
        return;
    }
    client->cacert = mqtt_ca;
}

/* Connect and immediately re-publish full state: the cloud only learns DP state
 * from device-initiated reports, so this runs after every (re)connect. */
static int mqtt_up(iot_client_t *client)
{
    int rc = iot_client_connect(client);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[iot] MQTT connect failed: %d\n", rc);
        return rc;
    }
    printf("[iot] MQTT connected; reporting full DP state\n");
    iot_dp_report_all(client);
    return OPRT_OK;
}

/* Bring the TAI session up, backing off between attempts. Returns 0 once
 * connected, -1 when the circuit breaker trips or the user interrupts.
 *
 * MUST run on the owning thread: tai_connect/tai_disconnect join the worker that
 * the receive callbacks run on. */
static int tai_link_up(tai_ctx_t *ctx, demo_reconnect_t *r)
{
    while (g_running) {
        int rc = tai_connect(ctx);
        if (rc == TAI_OK) {
            demo_reconnect_ok(r);
            return 0;
        }
        fprintf(stderr, "[tai] tai_connect failed: %d\n", rc);
        if (demo_reconnect_tripped(r)) {
            fprintf(stderr, "[tai] circuit breaker: giving up after %d attempts\n",
                    r->attempt);
            return -1;
        }
        uint32_t delay = demo_reconnect_delay_ms(r);
        fprintf(stderr, "[tai] retry in %u ms (attempt %d)\n", delay, r->attempt + 1);
        usleep(delay * 1000);
        r->attempt++;
        r->need_reconnect = 0;
    }
    return -1;
}

/* -- Reporting ------------------------------------------------------------ */

/* Report the DP the rule reads.
 * iot_dp_set validates against the schema, so a wrong id/type/range is reported
 * here instead of being silently rejected by the cloud. */
/* Wait `seconds` while still pumping the MQTT receive path.
 *
 * Deliberately deadline-driven rather than a fixed iteration count:
 * iot_client_process() DISCARDS its timeout argument (mqtt.c does
 * `(void)timeout_ms;`) and blocks for up to the compile-time
 * MQTT_RECV_TIMEOUT_MS -- 1000 ms, five times the 200 we pass. Counting
 * iterations therefore overshoots by 5x on an idle link. Overshoot here is
 * bounded by one recv budget instead. */
static void pump_for(iot_client_t *iot, int seconds)
{
    int64_t until = now_us() + (int64_t)seconds * 1000000LL;
    while (g_running && now_us() < until)
        iot_client_process(iot, 200);
}

static int report_state(iot_client_t *iot, const opts_t *o,
                        long battery, const char *what)
{
    /* Reported with the type the schema declares for this DP -- a bool DP takes
     * the value as 0/1, a value DP as the integer itself. */
    iot_dp_value_t level;
    char           shown[24];
    if (o->trigger_type == IOT_DP_TYPE_BOOL) {
        level.type          = IOT_DP_TYPE_BOOL;
        level.value.boolean = (battery != 0);
        snprintf(shown, sizeof(shown), "%s", level.value.boolean ? "true" : "false");
    } else {
        level.type          = IOT_DP_TYPE_VALUE;
        level.value.integer = (int32_t)battery;
        snprintf(shown, sizeof(shown), "%ld", battery);
    }
    int rc = iot_dp_set(iot, (uint8_t)DEFAULT_BATTERY_DP, &level);
    if (rc != OPRT_OK) {
        fprintf(stderr, "[dp] set DP %d = %s failed: %d\n",
                DEFAULT_BATTERY_DP, shown, rc);
        return rc;
    }

    rc = iot_dp_report_all_dirty(iot);
    printf("[dp] -> %s: DP %d = %s (rc=%d)\n",
           what, DEFAULT_BATTERY_DP, shown, rc);
    fflush(stdout);
    return rc;
}

/* -- main ----------------------------------------------------------------- */

int main(int argc, char **argv)
{
    opts_t o;
    int    prc = parse_opts(argc, argv, &o);
    if (prc != 0) return prc > 0 ? 0 : 1;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* Line-buffer stdout so the step-by-step log stays in order when the run is
     * redirected to a file: the SDKs log to stderr, which is unbuffered, and a
     * block-buffered stdout would interleave the two wrongly. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("=== tai_agent_trigger_demo ===\n");
    printf("Device ID : %s\n", DEFAULT_DEVID);
    printf("Region    : %s / %s\n", region_name(DEFAULT_REGION), env_name(DEFAULT_ENV));
    printf("Mode      : %s\n", o.listen_only ? "listen only (reports nothing)"
                                             : "report DPs, then wait for a push");
    fflush(stdout);   /* keep the banner ahead of anything the steps below log */

    /* ---- 1. Trigger DP type (before touching the network) ---------------- */
    /* Resolve the trigger DP's type from the schema and report it as that type.
     * iot_dp_set() rejects a mismatch with OPRT_DP_TYPE_MISMATCH, so this is
     * what keeps the demo working when DEFAULT_SCHEMA is edited to a product
     * whose battery DP is a bool rather than a numeric one. */
    const char *schema = DEFAULT_SCHEMA;
    if (!o.listen_only) {
        long dp_min = 0, dp_max = 100;
        if (schema_dp_type(schema, DEFAULT_BATTERY_DP, &o.trigger_type,
                           &dp_min, &dp_max) != 0)
            return 1;
        if (o.trigger_type != IOT_DP_TYPE_BOOL && o.trigger_type != IOT_DP_TYPE_VALUE) {
            fprintf(stderr, "DP %d is neither bool nor value; this demo cannot "
                            "drive it as a rule input\n", DEFAULT_BATTERY_DP);
            return 1;
        }
        /* 99/5 are percentages and both read as "true" for a bool DP, which
         * would report the same value twice and create no transition. Fall back
         * to false -> true unless the values were given explicitly. */
        if (o.trigger_type == IOT_DP_TYPE_BOOL) {
            if (!o.baseline_set) o.baseline = 0;
            if (!o.battery_set)  o.battery  = 1;
            o.use_mid = 0;   /* a random third bool would just repeat one of the two */
        }
        /* Pick the middle value inside the DP's own range, so it cannot be
         * rejected locally with OPRT_DP_VALUE_OUT_OF_RANGE. Note it is NOT kept
         * clear of the cloud's threshold -- that lives on the platform and is
         * unknown here -- so a draw below it fires the rule one step early. */
        if (o.use_mid && !o.mid_set) {
            struct timeval sd;
            gettimeofday(&sd, NULL);
            srand((unsigned)(sd.tv_sec ^ sd.tv_usec));
            long span = (dp_max > dp_min) ? (dp_max - dp_min + 1) : 1;
            /* Must differ from BOTH endpoints. Drawing the trigger value here
             * would make the final report carry no change at all, and the rule
             * would fire on this step while fire_us is stamped on the next one,
             * so every latency in the summary would be measured from the wrong
             * report. Drawing the baseline just wastes a step. */
            for (int tries = 0; tries < 16; tries++) {
                o.mid = dp_min + (long)(rand() % span);
                if (o.mid != o.battery && o.mid != o.baseline) break;
            }
            if (o.mid == o.battery || o.mid == o.baseline) {
                o.use_mid = 0;   /* range too narrow to fit a third value */
                printf("[dp] range %ld..%ld leaves no third value; skipping the "
                       "middle report\n", dp_min, dp_max);
            }
        }

        /* Print the plan before any network work: it is the only place the
         * random draw is visible, and it also shows which DP and which type
         * were resolved from the schema. */
        if (o.trigger_type == IOT_DP_TYPE_BOOL) {
            printf("Sequence  : DP %d (bool) %s -> %s\n", DEFAULT_BATTERY_DP,
                   o.baseline ? "true" : "false", o.battery ? "true" : "false");
        } else if (o.use_mid) {
            printf("Sequence  : DP %d (value, %ld..%ld) %ld -> %ld -> %ld\n",
                   DEFAULT_BATTERY_DP, dp_min, dp_max, o.baseline, o.mid, o.battery);
        } else {
            printf("Sequence  : DP %d (value, %ld..%ld) %ld -> %ld\n",
                   DEFAULT_BATTERY_DP, dp_min, dp_max, o.baseline, o.battery);
        }
        fflush(stdout);
    }

    /* ---- 2. IoT client -------------------------------------------------- */
    if (iot_init_default() != OPRT_OK) {
        fprintf(stderr, "iot_init_default failed\n");
        return 1;
    }
    log_set_level(o.verbose ? LOG_DEBUG : LOG_WARN);   /* both SDKs share the facade */

    iot_client_config_t iot_cfg = {
        .region            = DEFAULT_REGION,
        .env               = DEFAULT_ENV,
        .mqtt_disable_tls  = false,
        .mqtt_disable_auto_connect = true,   /* this demo owns the connect loop */
        .message_callback  = on_mqtt_message,
        .schema            = schema,
        .schema_id         = NULL,
        .dp_state          = NULL,
    };
    if (demo_copy_field(iot_cfg.devid,      sizeof(iot_cfg.devid),      DEFAULT_DEVID,      "devid")      != 0 ||
        demo_copy_field(iot_cfg.secret_key, sizeof(iot_cfg.secret_key), DEFAULT_SECRET_KEY, "secret_key") != 0 ||
        demo_copy_field(iot_cfg.local_key,  sizeof(iot_cfg.local_key),  DEFAULT_LOCAL_KEY,  "local_key")  != 0)
        return 1;

    iot_client_t *iot = iot_client_init(&iot_cfg);
    if (!iot) { fprintf(stderr, "iot_client_init failed\n"); return 1; }

    iot_dp_set_callback(iot, on_dp_downlink, NULL);

    /* ---- 3. Session token -> TAI connection params ---------------------- */
    char *token = (char *)calloc(1, 4096);
    if (!token) { iot_client_deinit(iot); return 1; }
    if (iot_client_get_session_token(iot, o.agent_code, token, 4096) != OPRT_OK ||
        token[0] == '\0') {
        fprintf(stderr, "iot_client_get_session_token failed\n");
        free(token);
        iot_client_deinit(iot);
        return 1;
    }

    tai_conn_params_t cp;
    if (parse_token(token, &cp) != 0) {
        fprintf(stderr, "token parse failed\n");
        free(token);
        iot_client_deinit(iot);
        return 1;
    }
    free(token);
    if (cp.biz_code == 0) cp.biz_code = 65537;
    if (cp.biz_tag  == 0) cp.biz_tag  = 119;

    printf("[tai] server    : %s:%u (SNI: %s)\n", cp.host, cp.port, cp.tls_sni);
    printf("[tai] client id : %s\n", cp.derived_client_id);

    /* ---- 4. TAI context ------------------------------------------------- */
    const pal_t *pal = tai_pal_posix();

    demo_ctx_t dc;
    memset(&dc, 0, sizeof(dc));

    if (o.audio_path && o.audio_path[0]) {
        dc.audio_fp = fopen(o.audio_path, "wb");
        if (!dc.audio_fp)
            fprintf(stderr, "[push] cannot open %s; TTS audio will be discarded\n",
                    o.audio_path);
    }

    /* session_attrs_json / event_user_data_json are left NULL: the SDK defaults
     * already declare deviceMcp and PCM 16 kHz TTS, and setting either one
     * REPLACES the default wholesale rather than merging into it. */
    tai_config_t tai_cfg = {
        .host             = cp.host,
        .port             = cp.port,
        .tls_sni          = cp.tls_sni,
        .device_id        = cp.derived_client_id,
        .local_key        = DEFAULT_LOCAL_KEY,
        .protocol_version = TAI_VER_21,
        .client_type      = TAI_CLIENT_DEVICE,
        .sign_level       = TAI_SIGN_HMAC_SHA256,
        .biz_code         = (uint32_t)cp.biz_code,
        .biz_tag          = (uint64_t)cp.biz_tag,
        .agent_token      = cp.agent_token,
        .pal              = pal,
        .on_text          = on_text,
        .on_audio         = on_audio,
        .on_event         = on_event,
        .on_disconnect    = on_disconnect,
        .user_data        = &dc,
    };

    void *ctx_buf = pal->malloc(tai_ctx_size());
    if (!ctx_buf) {
        fprintf(stderr, "OOM\n");
        if (dc.audio_fp) fclose(dc.audio_fp);
        iot_client_deinit(iot);
        return 1;
    }
    tai_ctx_t *ctx = tai_ctx_init(ctx_buf, &tai_cfg);
    if (!ctx) {
        fprintf(stderr, "tai_ctx_init failed\n");
        pal->free(ctx_buf);
        if (dc.audio_fp) fclose(dc.audio_fp);
        iot_client_deinit(iot);
        return 1;
    }

    int failed = 1;

    /* ---- 5. Open the AI session FIRST ---------------------------------- *
     * A trigger that fires while the device holds no session has nowhere to
     * push to, so the downlink half must be live before the uplink report. */
    printf("[tai] opening the AI session...\n");
    if (tai_link_up(ctx, &dc.reconn) != 0) goto cleanup;
    printf("[tai] session open; the demo sends nothing on it -- every turn from "
           "here on is server-initiated\n");

    /* ---- 6. MQTT up ---------------------------------------------------- */
    ensure_mqtt_ca(iot);
    if (mqtt_up(iot) != OPRT_OK) goto cleanup;

    /* ---- 7. Make the cloud rule match ---------------------------------- */
    if (!o.listen_only) {
        if (o.use_baseline) {
            /* The rule fires on a transition into the condition, so record a
             * healthy "before" state first. Skipping this step on a device the
             * cloud already believes is low simply produces no event. */
            if (report_state(iot, &o, o.baseline, "baseline") != OPRT_OK)
                goto cleanup;
            pump_for(iot, REPORT_INTERVAL_S);
        }
        if (o.use_mid) {
            /* A third, differing report between the two endpoints: it guarantees
             * the cloud sees a real change even when it already held the
             * baseline or the trigger value from an earlier run. */
            if (report_state(iot, &o, o.mid, "mid") != OPRT_OK)
                goto cleanup;
            pump_for(iot, REPORT_INTERVAL_S);
        }
        dc.fire_us = now_us();
        if (report_state(iot, &o, o.battery, "trigger") != OPRT_OK)
            goto cleanup;
    }

    printf("[main] waiting up to %d s for the agent to push (Ctrl-C to stop)\n",
           o.timeout_s);

    /* ---- 8. Wait ------------------------------------------------------- *
     * Two jobs on this one thread: pump the MQTT receive path (which also keeps
     * the broker link alive and dispatches downlink DP sets), and rebuild the
     * TAI session when the worker flags a drop. The TAI receive path itself runs
     * on the SDK's own thread. */
    unsigned seen     = 0;
    int64_t  deadline = now_us() + (int64_t)o.timeout_s * 1000000LL;

    int mqtt_fails = 0;

    while (g_running) {
        /* Checked FIRST. When the broker link flaps, the reconnect arm below
         * can take hundreds of ms per pass; checking the deadline last let a
         * flapping link run well past --timeout before the loop noticed. */
        if (now_us() > deadline) {
            if (seen == 0)
                fprintf(stderr, "[main] no push within %d s\n", o.timeout_s);
            else
                printf("[main] no further push within %d s\n", o.timeout_s);
            break;
        }

        int rc = iot_client_process(iot, 200);
        if (rc == OPRT_OK) {
            mqtt_fails = 0;
        } else {
            /* No auto-reconnect in the IoT SDK: reconnect, then re-report.
             * Back off on EVERY failed pass, not just a failed connect: a
             * broker that accepts the connection and then drops it immediately
             * would otherwise spin here reconnecting many times a second. */
            fprintf(stderr, "[iot] MQTT link error %d; reconnecting (%d)\n",
                    rc, mqtt_fails + 1);
            iot_client_disconnect(iot);
            if (++mqtt_fails >= MQTT_MAX_CONSECUTIVE_FAILS) {
                fprintf(stderr, "[iot] giving up on the broker after %d "
                                "consecutive failures\n", mqtt_fails);
                failed = 1;
                break;
            }
            uint32_t back_ms = 500u << (mqtt_fails - 1);   /* 0.5s,1s,2s,... */
            if (back_ms > 5000u) back_ms = 5000u;
            usleep(back_ms * 1000);
            mqtt_up(iot);
        }

        if (dc.reconn.need_reconnect) {
            /* Owning thread: safe to tear down and rebuild. Anything the trigger
             * pushed while the link was down is lost -- the session is the only
             * delivery path, and it has no replay. */
            tai_disconnect(ctx);
            demo_textbuf_reset(&dc.text);
            dc.turn_active = 0;
            dc.timeover    = 0;
            if (demo_reconnect_tripped(&dc.reconn)) {
                fprintf(stderr, "[tai] circuit breaker: giving up after %d attempts\n",
                        dc.reconn.attempt);
                break;
            }
            uint32_t delay = demo_reconnect_delay_ms(&dc.reconn);
            fprintf(stderr, "[tai] reconnect in %u ms (attempt %d)\n",
                    delay, dc.reconn.attempt + 1);
            usleep(delay * 1000);
            dc.reconn.attempt++;
            dc.reconn.need_reconnect = 0;
            if (tai_link_up(ctx, &dc.reconn) != 0) break;
            printf("[tai] session re-opened\n");
        }

        if (dc.pushes != seen) {
            seen     = dc.pushes;
            failed   = 0;
            deadline = now_us() + (int64_t)o.timeout_s * 1000000LL;
            if (o.repeat && seen >= o.repeat) break;
            printf("[main] %u push(es) received; waiting for more\n", seen);
        }

    }

cleanup:
    /* ---- 9. Shutdown --------------------------------------------------- */
    tai_disconnect(ctx);   /* joins the worker: dc is single-owner again */
    tai_ctx_deinit(ctx);
    pal->free(ctx_buf);

    iot_client_disconnect(iot);
    iot_client_deinit(iot);

    if (dc.audio_fp) fclose(dc.audio_fp);

    printf("\n--- summary ---\n");
    printf("pushes received : %u\n", dc.pushes);
    if (dc.pushes == 0) {
        /* Nothing arrived. The device half of this is verifiable from the log
         * above (session open, reports published), so point at the cloud-side
         * configuration rather than leaving a bare failure. */
        printf("no message was pushed. Check, on the Tuya platform:\n"
               "  - the device event rule exists AND is enabled for this product\n"
               "  - its DP conditions match what was reported above\n"
               "  - an agent trigger is bound to that event, task = push a message\n"
               "  - the product is associated with that agent\n"
               "  - the rule fires on a transition: re-reporting a value the cloud\n"
               "    already holds produces no event (see --baseline)\n");
    }
    if (dc.total_audio_bytes > 0 && o.audio_path && o.audio_path[0]) {
        printf("tts audio       : %s (%zu bytes, all turns concatenated)\n",
               o.audio_path, dc.total_audio_bytes);
        if (dc.audio_codec == TAI_AUDIO_PCM)
            printf("play with       : ffplay -f s16le -ar %u -ac 1 %s\n",
                   dc.audio_sample_rate ? dc.audio_sample_rate : 16000u,
                   o.audio_path);
    }

    /* A dropped text stream may well have been the pushed message; reporting
     * success for a run that lost its payload would mislead a caller. */
    if (dc.text.dropped) {
        fprintf(stderr, "%u text stream(s) were dropped; the pushed message may "
                        "have been among them\n", dc.text.dropped);
        failed = 1;
    }
    demo_textbuf_free(&dc.text);

    printf("%s\n", failed ? "FAILED" : "Done.");
    return failed ? 1 : 0;
}
