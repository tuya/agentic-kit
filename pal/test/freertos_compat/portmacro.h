/* Type-only host port for header checks, never used to run a kernel. */
#ifndef PAL_TEST_FREERTOS_COMPAT_PORTMACRO_H
#define PAL_TEST_FREERTOS_COMPAT_PORTMACRO_H

#include <stdint.h>
typedef uint32_t StackType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
#if TEST_TICK_BITS == 16
typedef uint16_t TickType_t;
#define portMAX_DELAY UINT16_MAX
#elif TEST_TICK_BITS == 32
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#else
typedef uint64_t TickType_t;
#define portMAX_DELAY UINT64_MAX
#endif
#define portSTACK_GROWTH (-1)
#define portBYTE_ALIGNMENT 8
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define portPRIVILEGE_BIT ((UBaseType_t)0)
#define portENTER_CRITICAL() ((void)0)
#define portEXIT_CRITICAL() ((void)0)

#endif
