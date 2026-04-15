#include "mbr.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void chs_max(uint8_t chs[3])
{
    chs[0] = 0xFE;
    chs[1] = 0xFF;
    chs[2] = 0xFF;
}

void rk_mbr_build_protective(uint8_t mbr[512], uint64_t total_sectors)
{
    memset(mbr, 0, 512);

    uint8_t *e = mbr + 0x1BE;
    e[0] = 0x00;                  /* not bootable */
    e[1] = 0x00; e[2] = 0x02; e[3] = 0x00; /* starting CHS: 0,0,2 */
    e[4] = 0xEE;                  /* type: GPT protective */
    chs_max(&e[5]);               /* ending CHS */
    put_le32(&e[8], 1);           /* first LBA = 1 */
    uint32_t span = (total_sectors > 0xFFFFFFFFu)
                  ? 0xFFFFFFFFu
                  : (uint32_t)(total_sectors - 1);
    put_le32(&e[12], span);

    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}

void rk_mbr_build_fat32(uint8_t mbr[512], uint64_t total_sectors,
                        uint32_t first_lba)
{
    memset(mbr, 0, 512);

    uint8_t *e = mbr + 0x1BE;
    e[0] = 0x00;
    e[1] = 0x20; e[2] = 0x21; e[3] = 0x00;
    e[4] = 0x0C;                  /* FAT32 LBA */
    chs_max(&e[5]);
    put_le32(&e[8], first_lba);
    uint64_t span64 = total_sectors - first_lba;
    uint32_t span = (span64 > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)span64;
    put_le32(&e[12], span);

    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}
