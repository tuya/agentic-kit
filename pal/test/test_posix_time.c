#include <errno.h>
#include <stdio.h>
#include <time.h>
#include "pal.h"

extern const pal_t *tai_pal_posix(void);

static struct timespec utc;
static struct timespec monotonic;
static int utc_error;
static unsigned passed;
static unsigned failed;

/* Renamed only in this target; the production PAL still calls clock_gettime. */
int pal_test_clock_gettime(clockid_t clock, struct timespec *value)
{
    if (clock == CLOCK_REALTIME) {
        if (utc_error) {
            errno = EIO;
            return -1;
        }
        *value = utc;
    } else if (clock == CLOCK_MONOTONIC) {
        *value = monotonic;
    } else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static void run_test(int (*test)(void))
{
    utc = (struct timespec){0, 0};
    monotonic = (struct timespec){3, 123456789};
    utc_error = 0;
    if (test() == 0) passed++;
    else failed++;
}

static int test_table(void)
{
    const pal_t *pal = tai_pal_posix();
    CHECK(pal_is_valid(pal));
    /* This target compiles the real port with optional LAN code disabled. */
    CHECK(!pal_has_server_network(pal));
    CHECK(!pal->tcp_listen && !pal->tcp_accept && !pal->udp_bind &&
          !pal->udp_sendto && !pal->udp_recvfrom && !pal->socket_local_port &&
          !pal->udp_close);
    pal_t incomplete = *pal;
    incomplete.wal_clock_time = NULL;
    CHECK(!pal_is_valid(&incomplete));
    incomplete = *pal;
    incomplete.time_ms = NULL;
    CHECK(!pal_is_valid(&incomplete));
    return 0;
}

static int test_utc_units_and_clock_domain(void)
{
    const pal_t *pal = tai_pal_posix();
    utc = (struct timespec){1700000000, 123456789};
    CHECK(pal->wal_clock_time() == 1700000000123ULL);
    CHECK(pal->time_ms() == 3123);
    return 0;
}

static int test_unavailable_utc(void)
{
    const pal_t *pal = tai_pal_posix();
    CHECK(pal->wal_clock_time() == 0);
    /* Subsecond values in the unset Unix epoch do not make UTC available. */
    utc.tv_nsec = 999000000;
    CHECK(pal->wal_clock_time() == 0);
    utc.tv_sec = -1;
    CHECK(pal->wal_clock_time() == 0);
    CHECK(pal->time_ms() == 3123);
    return 0;
}

static int test_failed_utc_read_and_recovery(void)
{
    const pal_t *pal = tai_pal_posix();
    utc = (struct timespec){1700000000, 0};
    utc_error = 1;
    CHECK(pal->wal_clock_time() == 0);
    CHECK(pal->time_ms() == 3123);
    utc_error = 0;
    CHECK(pal->wal_clock_time() == 1700000000000ULL);
    return 0;
}

static int test_utc_corrections_preserve_elapsed_time(void)
{
    const pal_t *pal = tai_pal_posix();
    uint64_t start = pal->time_ms();
    utc = (struct timespec){1800000000, 0};
    CHECK(pal->wal_clock_time() == 1800000000000ULL);
    CHECK(pal->time_ms() == start);
    utc.tv_sec = 1600000000;
    CHECK(pal->wal_clock_time() == 1600000000000ULL);
    CHECK(pal->time_ms() == start);
    /* Frozen UTC cannot freeze elapsed time or a deadline. */
    monotonic.tv_sec++;
    CHECK(pal->time_ms() - start == 1000);
    return 0;
}

int main(void)
{
    run_test(test_table);
    run_test(test_utc_units_and_clock_domain);
    run_test(test_unavailable_utc);
    run_test(test_failed_utc_read_and_recovery);
    run_test(test_utc_corrections_preserve_elapsed_time);
    printf("POSIX clock: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
