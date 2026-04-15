#include "disk.h"
#include <stdlib.h>

int rk_disk_zero(struct rk_disk *d, uint64_t offset, uint64_t len)
{
    if (len == 0) return 0;
    const size_t chunk = 1 << 20;
    uint8_t *buf = (uint8_t *)calloc(1, chunk);
    if (!buf) return -1;
    while (len > 0) {
        size_t w = len < chunk ? (size_t)len : chunk;
        if (rk_disk_write(d, offset, buf, w) != 0) { free(buf); return -1; }
        offset += w;
        len -= w;
    }
    free(buf);
    return 0;
}
