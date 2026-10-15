/* Host-only FreeRTOS type model for the PAL tick-clock regression matrix. */
#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H

#include <stdint.h>
#include <stdlib.h>

typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;

#if TEST_TICK_BITS == 16
typedef uint16_t TickType_t;
#elif TEST_TICK_BITS == 32
typedef uint32_t TickType_t;
#elif TEST_TICK_BITS == 64
typedef uint64_t TickType_t;
#else
#error "Unsupported test tick width"
#endif

#define configTICK_RATE_HZ ((uint32_t)TEST_TICK_HZ)
#define portMAX_DELAY ((TickType_t)~(TickType_t)0)
#define pdPASS 1

static inline void *pvPortMalloc(size_t size) { return malloc(size); }
static inline void vPortFree(void *pointer) { free(pointer); }

#endif
