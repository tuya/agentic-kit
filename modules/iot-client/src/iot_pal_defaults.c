#include "iot_internal.h"

extern const pal_t *tai_pal_posix(void);

const pal_t *get_default_pal(void)
{
    return tai_pal_posix();
}
