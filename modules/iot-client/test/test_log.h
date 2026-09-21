/*
 * test_log.h -- this binary's single log destination, fixed at compile
 * time: the log_config/agentic_kit_config.h next door remaps
 * AGENTIC_KIT_LOG to test_log_sink, so every TU of the test build --
 * SDK sources included -- dispatches into the one function below.
 *
 * What used to vary at runtime via log_set_handler() install/uninstall
 * is now a mode inside that one sink. Modes:
 *   PASSTHROUGH  default stderr output, unchanged shape
 *   CAPTURE      append lines to the registered buffer, keep stderr
 *                (a failing run stays readable)
 *   QUIET        swallow everything
 *   COUNT        count lines, no output
 *
 * The tuya-ble test executable shares this sink: it compiles the ble
 * sources into itself and links the iot test library that carries it.
 */
#ifndef IOT_TEST_LOG_H
#define IOT_TEST_LOG_H

#include <stddef.h>

#include "log.h"

typedef enum {
    TEST_LOG_PASSTHROUGH,
    TEST_LOG_CAPTURE,
    TEST_LOG_QUIET,
    TEST_LOG_COUNT,
} test_log_mode_t;

void test_log_set_mode(test_log_mode_t mode);

/* Capture window: lines append to buf (one per line, NUL-terminated,
 * truncation-safe) until test_log_capture_end() restores the mode that
 * was active before the window opened. */
void test_log_capture_begin(char *buf, size_t cap);
void test_log_capture_end(void);

/* COUNT mode's counter. */
void test_log_count_reset(void);
unsigned test_log_count_get(void);

/* The AGENTIC_KIT_LOG dispatch target for every TU of this test build. */
void test_log_sink(log_level_t level, const char *tag, const char *fmt, ...);

#endif /* IOT_TEST_LOG_H */
