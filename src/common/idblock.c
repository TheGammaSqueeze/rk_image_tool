#include "idblock.h"
#include "rc4.h"

#include <stdlib.h>
#include <string.h>

/*
 * Rockchip IDBlock builder.
 *
 * Reference behaviour matches rkflashtool / upgrade_tool / rkbin:
 *   - A RK "IDB" consists of N 512-byte sectors.
 *   - The header sector (sector 0) carries a fixed layout:
 *       off 0x00: magic "RK" (0x0ff0aa55 dword on little endian? varies).
 *       For the SD-Card path only the first 512 bytes need to contain the
 *       "FlashData" / "FlashBoot" entry table that the BootROM consumes to
 *       locate DDR init code and the SPL/uboot payload.
 *
 * Rather than hand-roll that table, we let the caller pass an already-formed
 * loader blob (the concatenated DDR+USBPLUG+FlashData+FlashBoot image that
 * Rockchip supplies as SDBoot.bin or MiniLoaderAll.bin). We:
 *   1. Round loader up to a multiple of 512.
 *   2. RC4 each 512-byte sector independently with the fixed key.
 *   3. Return the result.
 *
 * The BootROM decrypts each sector with the same RC4 key before executing.
 */

int rk_idb_build(const uint8_t *loader, uint64_t loader_len,
                 uint8_t **out, uint64_t *out_len)
{
    uint64_t rounded = (loader_len + RK_IDB_BLOCK_SIZE - 1) &
                       ~((uint64_t)RK_IDB_BLOCK_SIZE - 1);
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)rounded);
    if (!buf) return -1;
    memcpy(buf, loader, (size_t)loader_len);

    for (uint64_t off = 0; off < rounded; off += RK_IDB_BLOCK_SIZE) {
        rk_rc4(RK_RC4_KEY, sizeof(RK_RC4_KEY),
               buf + off, RK_IDB_BLOCK_SIZE);
    }
    *out = buf;
    *out_len = rounded;
    return 0;
}

int rk_idb_write(const uint8_t *loader, uint64_t loader_len,
                 rk_stream_write_fn write_fn, void *user)
{
    uint8_t *idb = NULL;
    uint64_t idb_len = 0;
    if (rk_idb_build(loader, loader_len, &idb, &idb_len) != 0) return -1;

    uint64_t off = (uint64_t)RK_IDB_SECTOR * RK_SECTOR_SIZE;
    int rc = write_fn(user, off, idb, (size_t)idb_len);
    free(idb);
    return rc;
}
