#include "pal.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern const pal_t *tai_pal_freertos(void);

/* Host storage models two byte-addressable pools and an unsuitable IRAM pool.
 * The latter satisfies INTERNAL alone, as on ESP32, but not 8BIT. */
typedef union { double alignment; unsigned char bytes[128]; } pool_t;
static pool_t psram, internal, iram;
static int psram_available, internal_available, iram_available;
static size_t requested_size, preference_count;
static uint32_t preferences[2];
static unsigned allocation_calls, free_calls, invalid_frees;
static void *live, *last_freed;
static unsigned passed, failed;

void vTaskSetTimeOutState(TimeOut_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
}

void *heap_caps_malloc_prefer(size_t size, size_t num, ...)
{
    va_list args;
    allocation_calls++;
    requested_size = size;
    preference_count = num;
    va_start(args, num);
    for (size_t i = 0; i < num; ++i) {
        uint32_t caps = va_arg(args, uint32_t);
        if (i < 2) preferences[i] = caps;
    }
    va_end(args);
    if (!size || size > sizeof(psram.bytes) || num != 2) return NULL;

    for (size_t i = 0; i < num; ++i) {
        uint32_t caps = preferences[i];
        if ((caps & MALLOC_CAP_SPIRAM) && psram_available) {
            live = psram.bytes;
            return live;
        }
        if (caps & MALLOC_CAP_INTERNAL) {
            if (!(caps & MALLOC_CAP_8BIT) && iram_available) {
                live = iram.bytes;
                return live;
            }
            if (internal_available) {
                live = internal.bytes;
                return live;
            }
        }
    }
    return NULL;
}

void heap_caps_free(void *ptr)
{
    free_calls++;
    last_freed = ptr;
    if (ptr && ptr != live) invalid_frees++;
    if (ptr == live) live = NULL;
}

static void reset_pools(int external, int data_ram, int instruction_ram)
{
    psram_available = external;
    internal_available = data_ram;
    iram_available = instruction_ram;
    requested_size = preference_count = 0;
    preferences[0] = preferences[1] = 0;
    allocation_calls = free_calls = invalid_frees = 0;
    live = last_freed = NULL;
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)
#define RUN_TEST(fn) do { if ((fn)() == 0) passed++; else failed++; } while (0)

static int test_psram_preference_is_byte_addressable(void)
{
    const pal_t *pal = tai_pal_freertos();
    reset_pools(1, 1, 1);
    CHECK(pal_is_valid(pal));
    void *ptr = pal->malloc(37);
    pal->free(ptr);
    CHECK(ptr == psram.bytes);
    CHECK(allocation_calls == 1 && requested_size == 37 && preference_count == 2);
    CHECK(preferences[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(preferences[1] == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    CHECK(!live && !invalid_frees);
    return 0;
}

static int test_internal_fallback_excludes_instruction_ram(void)
{
    const pal_t *pal = tai_pal_freertos();
    reset_pools(0, 1, 1);
    void *ptr = pal->malloc(37);
    pal->free(ptr);
    CHECK(ptr == internal.bytes);
    CHECK(!live && !invalid_frees);
    return 0;
}

static int test_unsuitable_remaining_memory_returns_null(void)
{
    const pal_t *pal = tai_pal_freertos();
    reset_pools(0, 0, 1);
    void *ptr = pal->malloc(37);
    if (ptr) pal->free(ptr);
    CHECK(ptr == NULL);
    CHECK(!live && !invalid_frees);
    return 0;
}

static int test_heap_caps_free_handles_both_pools_and_null(void)
{
    const pal_t *pal = tai_pal_freertos();
    for (int external = 0; external <= 1; ++external) {
        reset_pools(external, 1, 0);
        void *ptr = pal->malloc(37);
        CHECK(ptr != NULL);
        pal->free(ptr);
        CHECK(free_calls == 1 && last_freed == ptr && !live && !invalid_frees);
        pal->free(NULL);
        CHECK(free_calls == 2 && last_freed == NULL && !invalid_frees);
    }
    return 0;
}

int main(void)
{
    RUN_TEST(test_psram_preference_is_byte_addressable);
    RUN_TEST(test_internal_fallback_excludes_instruction_ram);
    RUN_TEST(test_unsuitable_remaining_memory_returns_null);
    RUN_TEST(test_heap_caps_free_handles_both_pools_and_null);
    printf("ESP-IDF PAL memory: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
