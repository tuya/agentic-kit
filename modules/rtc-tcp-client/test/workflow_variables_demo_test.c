/* Exercise the actual builder and callbacks; never run the networked main. */
#include <unistd.h>
#define main workflow_demo_main
#include "../../../examples/posix/ai/rtc-tcp-client/workflow_variables_demo.c"
#undef main

static int params_match(const char *json, const char *action, int32_t count)
{
    cJSON *root = json ? cJSON_ParseWithOpts(json, NULL, 1) : NULL;
    const cJSON *action_param = cJSON_GetObjectItemCaseSensitive(root, "action");
    const cJSON *count_param = cJSON_GetObjectItemCaseSensitive(root, "count");
    const cJSON *action_value = cJSON_GetObjectItemCaseSensitive(action_param, "value");
    const cJSON *count_value = cJSON_GetObjectItemCaseSensitive(count_param, "value");
    int matches = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 2 &&
        cJSON_IsObject(action_param) && cJSON_GetArraySize(action_param) == 1 &&
        cJSON_IsObject(count_param) && cJSON_GetArraySize(count_param) == 1 &&
        cJSON_IsString(action_value) && strcmp(action_value->valuestring, action) == 0 &&
        cJSON_IsNumber(count_value) && count_value->valuedouble == (double)count;
    cJSON_Delete(root);
    return matches;
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_lock(void *mutex) { (void)mutex; }
static uint64_t test_time(void) { return 1; }

static int test_workflow_output(void)
{
    int failures = 0;
    FILE *capture = tmpfile();
    int saved_stdout = dup(STDOUT_FILENO);
    if (!capture || saved_stdout < 0) {
        if (capture) fclose(capture);
        if (saved_stdout >= 0) close(saved_stdout);
        return 1;
    }
    fflush(stdout);
    if (dup2(fileno(capture), STDOUT_FILENO) < 0) {
        fclose(capture);
        close(saved_stdout);
        return 1;
    }
    const pal_t pal = {
        .mutex_lock = test_lock, .mutex_unlock = test_lock, .time_ms = test_time
    };
    demo_ctx_t dc = {
        .pal = &pal, .action = "decide", .active = 1, .deadline = 100
    };
    const char *chunks[] = {
        "{\"bizType\":\"NLG\",\"data\":{\"content\":\"{\\n\"}}",
        "{\"bizType\":\"NLG\",\"data\":{\"content\":\"  \\\"result\\\": true\\n\"}}",
        "{\"bizType\":\"NLG\",\"data\":{\"content\":\"}\\n\"}}",
        "{\"bizType\":\"NLG\",\"eof\":1,\"data\":{\"content\":\"\",\"finish\":true}}"
    };
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        /* SDK callback slices are not NUL-terminated. */
        size_t len = strlen(chunks[i]);
        char *slice = malloc(len);
        memcpy(slice, chunks[i], len);
        tai_text_msg_t msg = {
            .text = slice, .len = len, .seq = (uint32_t)i + 1,
            .stream_flag = i ? TAI_STREAM_MIDDLE : TAI_STREAM_START
        };
        on_text(NULL, &msg, &dc);
        free(slice);
        CHECK(!dc.failed && !dc.done && !dc.output.done);
    }
    tai_event_msg_t end = {.event_type = TAI_EVT_END};
    on_event(NULL, &end, &dc);
    CHECK(dc.done && !dc.failed && !dc.active);
    CHECK(strcmp(dc.output.buf, "{\n  \"result\": true\n}\n") == 0);

    const struct {
        const char *json;
        int valid;
    } outputs[] = {
        {"{\"result\":false}", 1},
        {"{\"result\":true,\"note\":\"accepted\"}", 1},
        {"{\"type\":\"card\",\"action\":\"move\",\"count\":3}", 0},
        {"{\"result\":\"true\"}", 0},
        {"{\"result\":1}", 0},
        {"{\"result\":null}", 0},
        {"{\"result\":true", 0},
        {"{\"result\":true}\ntrailing output", 0},
        {"[]", 0},
        {"   ", 0}
    };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); i++) {
        dc.active = 1;
        dc.done = dc.meaningful = dc.failed = 0;
        demo_textbuf_reset(&dc.text);
        demo_textbuf_reset(&dc.output);
        tai_text_msg_t msg = {
            .text = outputs[i].json, .len = strlen(outputs[i].json),
            .stream_flag = TAI_STREAM_ONE_SHOT, .seq = 1
        };
        on_text(NULL, &msg, &dc);
        on_event(NULL, &end, &dc);
        CHECK(dc.done && !dc.active && dc.failed == !outputs[i].valid);
    }
    demo_textbuf_free(&dc.text);
    demo_textbuf_free(&dc.output);
    fflush(stdout);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    rewind(capture);
    char printed[8192];
    size_t n = fread(printed, 1, sizeof(printed) - 1, capture);
    printed[n] = '\0';
    CHECK(strstr(printed, "[Workflow JSON parsed]\n") != NULL);
    CHECK(strstr(printed, "  result (boolean): true\n") != NULL);
    CHECK(strstr(printed, "  result (boolean): false\n") != NULL);
    CHECK(strstr(printed, "  note (string): \"accepted\"\n") != NULL);
    fclose(capture);
    return failures;
}

static int test_response_types(void)
{
    int failures = 0;
    const pal_t pal = {
        .mutex_lock = test_lock, .mutex_unlock = test_lock, .time_ms = test_time
    };
    demo_ctx_t dc = {
        .pal = &pal, .action = "chat", .active = 1, .deadline = 100
    };
    /* Plain chat is complete text; it must not need a decide JSON result. */
    const char text[] = {'h', 'e', 'l', 'l', 'o'};
    tai_text_msg_t msg = {
        .text = text, .len = sizeof(text),
        .stream_flag = TAI_STREAM_ONE_SHOT, .seq = 1
    };
    tai_event_msg_t end = {.event_type = TAI_EVT_END};
    on_text(NULL, &msg, &dc);
    on_event(NULL, &end, &dc);
    CHECK(dc.done && !dc.failed && !dc.active);
    CHECK(dc.output.len == sizeof(text) &&
          memcmp(dc.output.buf, text, sizeof(text)) == 0);
    demo_textbuf_free(&dc.text);
    demo_textbuf_free(&dc.output);

    dc = (demo_ctx_t){
        .pal = &pal, .action = "generate-img", .active = 1, .deadline = 100
    };
    /* These callbacks only buffer the borrowed image bytes; no files are saved. */
    const uint8_t image[] = {0x89, 'P', 'N', 'G'};
    tai_image_msg_t image_msg = {
        .data = image, .len = sizeof(image), .format = TAI_IMG_PNG,
        .stream_flag = TAI_STREAM_ONE_SHOT
    };
    on_image(NULL, &image_msg, &dc);
    CHECK(dc.image_done && !dc.image_started && !dc.done && !dc.failed);
    on_event(NULL, &end, &dc);
    CHECK(dc.done && !dc.failed && !dc.active);
    CHECK(dc.image_len == sizeof(image) && dc.image_format == TAI_IMG_PNG &&
          memcmp(dc.image, image, sizeof(image)) == 0);
    free(dc.image);

    dc = (demo_ctx_t){
        .pal = &pal, .action = "generate-img", .active = 1, .deadline = 100
    };
    on_event(NULL, &end, &dc);
    CHECK(dc.done && dc.failed && !dc.active);

    dc = (demo_ctx_t){
        .pal = &pal, .action = "generate-img", .active = 1, .deadline = 100
    };
    image_msg.stream_flag = TAI_STREAM_START;
    on_image(NULL, &image_msg, &dc);
    CHECK(dc.image_started && !dc.image_done && !dc.failed);
    on_event(NULL, &end, &dc);
    CHECK(dc.done && dc.failed && !dc.active);
    free(dc.image);
    return failures;
}

int main(void)
{
    int failures = 0;
    const struct {
        const char *action;
        int32_t count;
    } cases[] = {
        {"sing", 2},
        {"dance", 3},
        {"quote\" slash\\ newline\n tab\t return\r backspace\b formfeed\f", 0},
        {"minimum", INT32_MIN},
        {"maximum", INT32_MAX},
    };
    char *json[sizeof(cases) / sizeof(cases[0])] = {0};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        json[i] = build_workflow_params(cases[i].action, cases[i].count);
        CHECK(params_match(json[i], cases[i].action, cases[i].count));
        /* Later values must replace both fields without mutating earlier results. */
        for (size_t j = 0; j < i; j++)
            CHECK(params_match(json[j], cases[j].action, cases[j].count));
    }
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        cJSON_free(json[i]);

    /* The same oracle must reject the original server-incompatible shape. */
    CHECK(!params_match("{\"action\":\"sing\",\"count\":2}", "sing", 2));
    CHECK(!params_match("{\"action\":{\"value\":\"sing\"},\"count\":{\"value\":\"2\"}}",
                        "sing", 2));
    failures += test_workflow_output();
    failures += test_response_types();
    printf("workflow_variables_demo_test: %d failures\n", failures);
    return failures ? 1 : 0;
}
