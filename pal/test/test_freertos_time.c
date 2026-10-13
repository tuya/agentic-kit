#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pal.h"

extern const pal_t *tai_pal_freertos(void);

static struct timeval wall_time;
static int wall_error;

/* Renamed only in this test target; production still calls libc gettimeofday. */
int pal_test_gettimeofday(struct timeval *value, void *timezone)
{
    (void)timezone;
    if (wall_error) return -1;
    *value = wall_time;
    return 0;
}

static void set_wall_ms(uint64_t unix_ms)
{
    wall_error = 0;
    wall_time.tv_sec = (time_t)(unix_ms / 1000U);
    wall_time.tv_usec = (suseconds_t)(unix_ms % 1000U) * 1000;
}

static TimeOut_t kernel_time;
static unsigned passed;
static unsigned failed;

/* Model the coherent snapshot returned by the kernel's critical section. */
void vTaskSetTimeOutState(TimeOut_t *state)
{
    *state = kernel_time;
}

static void set_ticks(uint64_t ticks)
{
    kernel_time.xTimeOnEntering = (TickType_t)ticks;
#if TEST_TICK_BITS < 64
    kernel_time.xOverflowCount = (BaseType_t)(ticks >> TEST_TICK_BITS);
#else
    kernel_time.xOverflowCount = 0;
#endif
}

/* Host compilers support a wider reference product than the adapter uses. */
static uint64_t expected_time(uint64_t ticks)
{
    __uint128_t value = (__uint128_t)ticks * 1000U / TEST_TICK_HZ;
    return value > UINT64_MAX ? UINT64_MAX : (uint64_t)value;
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    if ((fn)() == 0) { passed++; } else { failed++; } \
} while (0)

static uint64_t wrap_boundary(void)
{
#if TEST_TICK_BITS < 64
    return UINT64_C(1) << TEST_TICK_BITS;
#else
    /* A native 64-bit clock must also cross the old 32-bit boundary. */
    return UINT64_C(1) << 32;
#endif
}

static int test_units_and_resolution(void)
{
    CHECK(pal_is_valid(tai_pal_freertos()));
    set_ticks(1);
    CHECK(tai_pal_freertos()->time_ms() == expected_time(1));
    set_ticks(TEST_TICK_HZ);
    CHECK(tai_pal_freertos()->time_ms() == 1000);
    return 0;
}

static int test_repeated_reads(void)
{
    set_ticks(7);
    uint64_t ms = tai_pal_freertos()->time_ms();
    CHECK(tai_pal_freertos()->time_ms() == ms);
    return 0;
}

static int test_tick_wrap(void)
{
    uint64_t boundary = wrap_boundary();
    set_ticks(boundary - 16);
    uint64_t before = tai_pal_freertos()->time_ms();
    set_ticks(boundary + 32);
    uint64_t after = tai_pal_freertos()->time_ms();
    CHECK(after > before);
    CHECK(after - before == expected_time(boundary + 32)
                          - expected_time(boundary - 16));
    return 0;
}

static int test_multiple_wraps_between_reads(void)
{
    uint64_t ticks = 5 * wrap_boundary() + 123;
    set_ticks(123);
    uint64_t before = tai_pal_freertos()->time_ms();
    set_ticks(ticks);
    CHECK(tai_pal_freertos()->time_ms() == expected_time(ticks));
    CHECK(tai_pal_freertos()->time_ms() > before);
    return 0;
}

static int test_read_after_prior_wraps(void)
{
    /* Correct absolute time without any per-reader init or sampling history. */
    uint64_t ticks = 3 * wrap_boundary() + TEST_TICK_HZ;
    set_ticks(ticks);
    CHECK(tai_pal_freertos()->time_ms() == expected_time(ticks));
    return 0;
}

static int test_connection_deadline_across_wrap(void)
{
    uint64_t boundary = wrap_boundary();
    set_ticks(boundary - 16);
    uint64_t start = tai_pal_freertos()->time_ms();
    uint64_t deadline = start + 1000;
    set_ticks(boundary + 16);
    CHECK(tai_pal_freertos()->time_ms() < deadline);
    CHECK(tai_pal_freertos()->time_ms() - start < 1000);
    set_ticks(boundary + TEST_TICK_HZ + 16);
    CHECK(tai_pal_freertos()->time_ms() >= deadline);
    return 0;
}

static int test_large_conversion(void)
{
#if TEST_TICK_BITS < 64
    uint64_t ticks = ((uint64_t)INT32_MAX << TEST_TICK_BITS)
                   | (wrap_boundary() - 1);
#else
    uint64_t ticks = UINT64_MAX;
#endif
    set_ticks(ticks);
    CHECK(tai_pal_freertos()->time_ms() == expected_time(ticks));
    return 0;
}

static int test_wall_clock_unavailable(void)
{
    set_wall_ms(0);
    CHECK(tai_pal_freertos()->wal_clock_time() == 0);
    return 0;
}

static int test_wall_clock_uses_utc_ms(void)
{
    const uint64_t epoch = 1700000000123ULL;
    set_ticks(TEST_TICK_HZ);
    set_wall_ms(epoch);
    wall_time.tv_usec += 456;  /* Fractional milliseconds are truncated. */
    CHECK(tai_pal_freertos()->wal_clock_time() == epoch);
    CHECK(tai_pal_freertos()->time_ms() == 1000);
    return 0;
}

static int test_wall_clock_steps_preserve_monotonic(void)
{
    set_ticks(3 * TEST_TICK_HZ);
    uint64_t start = tai_pal_freertos()->time_ms();
    set_wall_ms(1800000000000ULL);
    CHECK(tai_pal_freertos()->wal_clock_time() == 1800000000000ULL);
    CHECK(tai_pal_freertos()->time_ms() == start);
    set_wall_ms(1600000000000ULL);
    CHECK(tai_pal_freertos()->wal_clock_time() == 1600000000000ULL);
    CHECK(tai_pal_freertos()->time_ms() == start);
    set_ticks(4 * TEST_TICK_HZ);
    CHECK(tai_pal_freertos()->time_ms() - start == 1000);
    return 0;
}

static int test_wall_clock_read_failure(void)
{
    set_ticks(TEST_TICK_HZ);
    set_wall_ms(1700000000000ULL);
    wall_error = 1;
    CHECK(tai_pal_freertos()->wal_clock_time() == 0);
    CHECK(tai_pal_freertos()->time_ms() == 1000);
    wall_error = 0;
    CHECK(tai_pal_freertos()->wal_clock_time() == 1700000000000ULL);
    return 0;
}

int main(void)
{
    RUN_TEST(test_units_and_resolution);
    RUN_TEST(test_repeated_reads);
    RUN_TEST(test_tick_wrap);
    RUN_TEST(test_multiple_wraps_between_reads);
    RUN_TEST(test_read_after_prior_wraps);
    RUN_TEST(test_connection_deadline_across_wrap);
    RUN_TEST(test_large_conversion);
    RUN_TEST(test_wall_clock_unavailable);
    RUN_TEST(test_wall_clock_uses_utc_ms);
    RUN_TEST(test_wall_clock_steps_preserve_monotonic);
    RUN_TEST(test_wall_clock_read_failure);
    printf("FreeRTOS clock (%d-bit ticks, %d Hz): %u passed, %u failed\n",
           TEST_TICK_BITS, TEST_TICK_HZ, passed, failed);
    return failed ? 1 : 0;
}
