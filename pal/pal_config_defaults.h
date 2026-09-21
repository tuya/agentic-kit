/*
 * pal_config_defaults.h -- PAL build-time knobs: the FreeRTOS + lwIP worker
 * task sizing (pal/pal_freertos.c). The POSIX PAL (pal_posix.c) has no
 * knobs -- pthreads take the platform default stack.
 *
 * pal_freertos.c includes this file directly. It pulls common/log.h FIRST:
 * that header is where integrator overrides (agentic_kit_config.h on the
 * include path, -D, AGENTIC_KIT_USER_CONFIG) are applied and the SDK-wide
 * log ceiling defaults -- so overrides win over every default below. Not a
 * public API header.
 */

#ifndef AGENTIC_KIT_PAL_CONFIG_DEFAULTS_H
#define AGENTIC_KIT_PAL_CONFIG_DEFAULTS_H

#include "log.h"

/* =========================================================================
 * PAL: FreeRTOS + lwIP worker task (pal/pal_freertos.c)
 * ========================================================================= */

/* Worker task stack, in StackType_t WORDS (not bytes) -- passed straight to
 * xTaskCreate as usStackDepth. The 6144 default is ~24 KB on a 32-bit port;
 * do not "correct" it to 1536 to get 6 KB, the TLS handshake runs on this
 * task. */
#ifndef AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS
#define AGENTIC_KIT_PAL_FR_TASK_STACK_WORDS  6144
#endif

/* Worker task priority. The default expands at its use site in
 * pal_freertos.c, where the FreeRTOS headers are in scope -- keep
 * tskIDLE_PRIORITY unquoted here. */
#ifndef AGENTIC_KIT_PAL_FR_TASK_PRIORITY
#define AGENTIC_KIT_PAL_FR_TASK_PRIORITY     (tskIDLE_PRIORITY + 5)
#endif

/* Worker task name string (debug only). */
#ifndef AGENTIC_KIT_PAL_FR_TASK_NAME
#define AGENTIC_KIT_PAL_FR_TASK_NAME         "tai_worker"
#endif

#endif /* AGENTIC_KIT_PAL_CONFIG_DEFAULTS_H */
