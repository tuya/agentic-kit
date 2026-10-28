#include "bounded_string.h"

#include <stdio.h>

static unsigned passed, failed, total;
#define CHECK(expr) do { if (!(expr)) { \
    printf("%s:%d: %s\n", __FILE__, __LINE__, #expr); return -1; \
} } while (0)
#define RUN(fn) do { ++total; if (fn() == 0) ++passed; else ++failed; } while (0)

static int test_invalid_arguments(void)
{
    CHECK(!bounded_string_nonempty(NULL, 0));
    CHECK(!bounded_string_nonempty(NULL, 4));
    CHECK(!bounded_string_nonempty("x", 0));
    CHECK(!bounded_string_nonempty("", 1));
    CHECK(!bounded_string_nonempty("x", 1));
    return 0;
}

static int test_terminated_boundaries(void)
{
    const char exact[] = {'a', 'b', '\0'};
    const char early[] = {'a', '\0', 'x'};
    CHECK(bounded_string_nonempty(exact, sizeof(exact)));
    CHECK(!bounded_string_nonempty(exact, sizeof(exact) - 1));
    CHECK(bounded_string_nonempty(early, sizeof(early)));
    CHECK(bounded_string_nonempty(early, 2));
    return 0;
}

static int test_unterminated_slices(void)
{
    /* No sentinel follows either complete object; sanitizers catch overreads. */
    const char one[] = {'x'};
    const char raw[] = {'a', 'b', 'c'};
    CHECK(!bounded_string_nonempty(one, sizeof(one)));
    CHECK(!bounded_string_nonempty(raw, sizeof(raw)));
    CHECK(!bounded_string_nonempty(raw, 0));
    return 0;
}

int main(void)
{
    RUN(test_invalid_arguments);
    RUN(test_terminated_boundaries);
    RUN(test_unterminated_slices);
    printf("=== Bounded string: %u passed, %u failed (%u total) ===\n",
           passed, failed, total);
    return failed ? 1 : 0;
}
