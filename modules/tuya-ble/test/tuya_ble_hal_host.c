#include "tuya_ble_prov.h"

#if defined(__APPLE__)
#include <stdlib.h>

void tuya_ble_hal_random(uint8_t *buf, size_t len)
{
    arc4random_buf(buf, len);
}
#elif defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

void tuya_ble_hal_random(uint8_t *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return;
    }

    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, buf + off, len - off);
        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(fd);
}
#else
#error "Provide a platform-specific tuya_ble_hal_random implementation or disable TUYA_BLE_USE_HOST_HAL"
#endif
