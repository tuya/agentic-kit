#ifndef PAL_TEST_ESP_HEAP_CAPS_H
#define PAL_TEST_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

/* ESP-IDF capability values used by the production PAL allocation branch. */
#define MALLOC_CAP_8BIT     (1U << 2)
#define MALLOC_CAP_SPIRAM   (1U << 10)
#define MALLOC_CAP_INTERNAL (1U << 11)

void *heap_caps_malloc_prefer(size_t size, size_t num, ...);
void heap_caps_free(void *ptr);

#endif
