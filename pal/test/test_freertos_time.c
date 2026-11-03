#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pal.h"

extern const pal_t *tai_pal_freertos(void);

static uint64_t application_utc_ms;
static struct timeval platform_time;
static int platform_read_result;
static unsigned platform_read_calls;
static int platform_timezone_requested;

/* The test build remaps the platform read in the actual default callback. */
int pal_test_gettimeofday(struct timeval *tv, void *tz)
{
    platform_read_calls++;
    if (tz != NULL) platform_timezone_requested = 1;
    if (platform_read_result != 0) return platform_read_result;
    *tv = platform_time;
    return 0;
}

static void set_platform_time(time_t seconds, suseconds_t microseconds)
{
    platform_time.tv_sec = seconds;
    platform_time.tv_usec = microseconds;
    platform_read_result = 0;
}

static uint64_t application_wall_ms(void)
{
    return application_utc_ms;
}

static pal_t application_pal(void)
{
    pal_t pal = *tai_pal_freertos();
    pal.wal_clock_time = application_wall_ms;
    return pal;
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
    /* Clock targets compile the real port with optional LAN code disabled. */
    const pal_t *pal = tai_pal_freertos();
    CHECK(!pal_has_server_network(pal));
    CHECK(!pal->tcp_listen && !pal->tcp_accept && !pal->udp_bind &&
          !pal->udp_sendto && !pal->udp_recvfrom && !pal->socket_local_port &&
          !pal->udp_close);
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

static int test_default_utc_uses_ms(void)
{
    const pal_t *pal = tai_pal_freertos();
    CHECK(pal_is_valid(pal));
    platform_read_calls = 0;
    platform_timezone_requested = 0;
    set_platform_time(1700000000, 123456);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000000123));
    set_platform_time(1700000000, 999);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000000000));
    set_platform_time(1700000000, 999999);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000000999));
    set_platform_time(1700000001, 0);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000001000));
    CHECK(platform_read_calls == 4);
    CHECK(platform_timezone_requested == 0);
    return 0;
}

static int test_default_utc_unavailable(void)
{
    const pal_t *pal = tai_pal_freertos();
    set_platform_time(0, 0);
    CHECK(pal->wal_clock_time() == 0);
    set_platform_time(0, 999999);
    CHECK(pal->wal_clock_time() == 0);
    set_platform_time(-1, 999999);
    CHECK(pal->wal_clock_time() == 0);
    return 0;
}

static int test_default_utc_read_failure_and_recovery(void)
{
    const pal_t *pal = tai_pal_freertos();
    platform_read_calls = 0;
    set_platform_time(1700000000, 123000);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000000123));
    platform_read_result = -1;
    CHECK(pal->wal_clock_time() == 0);
    set_platform_time(1700000001, 456000);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000001456));
    CHECK(platform_read_calls == 3);
    return 0;
}

static int test_default_utc_steps_preserve_monotonic(void)
{
    const pal_t *pal = tai_pal_freertos();
    set_ticks(3 * TEST_TICK_HZ);
    uint64_t start = pal->time_ms();
    set_platform_time(1700000000, 123000);
    CHECK(pal->wal_clock_time() == UINT64_C(1700000000123));
    set_platform_time(1800000000, 456000);
    CHECK(pal->wal_clock_time() == UINT64_C(1800000000456));
    CHECK(pal->time_ms() == start);
    set_platform_time(1600000000, 789000);
    CHECK(pal->wal_clock_time() == UINT64_C(1600000000789));
    CHECK(pal->time_ms() == start);
    set_ticks(4 * TEST_TICK_HZ);
    CHECK(pal->time_ms() - start == 1000);
    CHECK(pal->wal_clock_time() == UINT64_C(1600000000789));
    return 0;
}

static int test_injected_utc_unavailable(void)
{
    pal_t pal = application_pal();
    application_utc_ms = 0;
    platform_read_calls = 0;
    platform_read_result = -1;
    CHECK(pal_is_valid(&pal));
    CHECK(pal.wal_clock_time() == 0);
    CHECK(platform_read_calls == 0);
    return 0;
}

static int test_injected_utc_uses_ms(void)
{
    pal_t pal = application_pal();
    set_ticks(TEST_TICK_HZ);
    application_utc_ms = 1700000000123ULL;
    platform_read_calls = 0;
    platform_read_result = -1;
    CHECK(pal.wal_clock_time() == 1700000000123ULL);
    CHECK(pal.time_ms() == 1000);
    CHECK(pal.wal_clock_time != tai_pal_freertos()->wal_clock_time);
    CHECK(platform_read_calls == 0);
    return 0;
}

static int test_injected_utc_steps_preserve_monotonic(void)
{
    pal_t pal = application_pal();
    set_ticks(3 * TEST_TICK_HZ);
    uint64_t start = pal.time_ms();
    application_utc_ms = 1800000000000ULL;
    CHECK(pal.wal_clock_time() == application_utc_ms);
    CHECK(pal.time_ms() == start);
    application_utc_ms = 1600000000000ULL;
    CHECK(pal.wal_clock_time() == application_utc_ms);
    CHECK(pal.time_ms() == start);
    set_ticks(4 * TEST_TICK_HZ);
    CHECK(pal.time_ms() - start == 1000);
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
    RUN_TEST(test_default_utc_uses_ms);
    RUN_TEST(test_default_utc_unavailable);
    RUN_TEST(test_default_utc_read_failure_and_recovery);
    RUN_TEST(test_default_utc_steps_preserve_monotonic);
    RUN_TEST(test_injected_utc_unavailable);
    RUN_TEST(test_injected_utc_uses_ms);
    RUN_TEST(test_injected_utc_steps_preserve_monotonic);
    printf("FreeRTOS clock (%d-bit ticks, %d Hz): %u passed, %u failed\n",
           TEST_TICK_BITS, TEST_TICK_HZ, passed, failed);
    return failed ? 1 : 0;
}
