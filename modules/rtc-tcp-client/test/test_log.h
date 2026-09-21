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
 */
#ifndef TAI_TEST_LOG_H
#define TAI_TEST_LOG_H

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

/* Integration-suite default: quiet unless TAI_LOOPBACK_VERBOSE is set
 * (and not "0") -- the gate tai_pal_loopback's lb_log used to apply. */
void test_log_env_default(void);

/* The AGENTIC_KIT_LOG dispatch target for every TU of this test build. */
void test_log_sink(log_level_t level, const char *tag, const char *fmt, ...);

#endif /* TAI_TEST_LOG_H */
