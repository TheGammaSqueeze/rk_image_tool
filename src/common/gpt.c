#include "gpt.h"
#include "crc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static void put_le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (i*8));
}

static uint64_t rk_rng_state;
static void rng_seed(void)
{
    if (rk_rng_state) return;
    rk_rng_state = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&rk_rng_state;
    if (!rk_rng_state) rk_rng_state = 0x9E3779B97F4A7C15ULL;
}
static uint64_t rng_next(void)
{
    rk_rng_state ^= rk_rng_state << 13;
    rk_rng_state ^= rk_rng_state >> 7;
    rk_rng_state ^= rk_rng_state << 17;
    return rk_rng_state;
}
static void gen_guid(uint8_t g[16])
{
    rng_seed();
    uint64_t a = rng_next();
    uint64_t b = rng_next();
    for (int i = 0; i < 8; ++i) g[i]   = (uint8_t)(a >> (i*8));
    for (int i = 0; i < 8; ++i) g[8+i] = (uint8_t)(b >> (i*8));
    g[7] = (uint8_t)((g[7] & 0x0F) | 0x40); /* v4 */
    g[8] = (uint8_t)((g[8] & 0x3F) | 0x80);
}

/* Microsoft Basic Data partition type GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
 * Stored little-endian mixed (first three fields LE, last two BE):
 *   A2 A0 D0 EB E5 B9 33 44 87 C0 68 B6 B7 26 99 C7 */
static const uint8_t MS_BASIC_DATA_GUID[16] = {
    0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44,
    0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7
};

static void ascii_to_utf16le(const char *s, uint8_t *out, size_t max_chars)
{
    size_t i = 0;
    for (; i < max_chars && s[i]; ++i) {
        out[i*2] = (uint8_t)s[i];
        out[i*2+1] = 0;
    }
    for (; i < max_chars; ++i) {
        out[i*2] = 0;
        out[i*2+1] = 0;
    }
}

/* Round a sector count up to 64-sector (32 KiB) boundary. */
static uint64_t round_up_64(uint64_t x)
{
    return (x + 63) & ~(uint64_t)63;
}

int rk_gpt_build(const struct rk_parameter *params, uint64_t total_sectors,
                 uint8_t *primary_hdr,
                 uint8_t *primary_entries,
                 uint8_t *backup_hdr,
                 uint8_t *backup_entries)
{
    if (total_sectors < 68) return -1;

    size_t ent_bytes = RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE;
    memset(primary_hdr, 0, 512);
    memset(backup_hdr, 0, 512);
    memset(primary_entries, 0, ent_bytes);
    memset(backup_entries, 0, ent_bytes);

    uint64_t first_usable = 34;
    uint64_t last_usable  = total_sectors - 34;
    last_usable &= ~(uint64_t)63ull;
    if (last_usable > 0) last_usable -= 1;

    for (size_t i = 0; i < params->num_parts && i < RK_GPT_ENTRIES; ++i) {
        const struct rk_part_entry *p = &params->parts[i];
        uint8_t *e = primary_entries + i * RK_GPT_ENTRY_SIZE;

        memcpy(e + 0x00, MS_BASIC_DATA_GUID, 16);
        gen_guid(e + 0x10);

        uint64_t first_lba = p->offset_lba;
        uint64_t last_lba;
        if (p->grow) {
            last_lba = last_usable;
        } else {
            uint64_t end = (uint64_t)p->offset_lba + (uint64_t)p->size_lba;
            if (p->size_lba == 0) end = first_lba + 1;
            last_lba = round_up_64(end) - 1;
            if (last_lba > last_usable) last_lba = last_usable;
        }
        if (first_lba < first_usable) first_lba = first_usable;
        if (last_lba < first_lba) last_lba = first_lba;

        put_le64(e + 0x20, first_lba);
        put_le64(e + 0x28, last_lba);
        put_le64(e + 0x30, 0);
        ascii_to_utf16le(p->name, e + 0x38, 36);
    }

    uint32_t ent_crc = crc32_ieee(0, primary_entries, ent_bytes);

    uint8_t disk_guid[16];
    gen_guid(disk_guid);

    /* Primary header at LBA 1 */
    memcpy(primary_hdr + 0, "EFI PART", 8);
    put_le32(primary_hdr + 8, 0x00010000);
    put_le32(primary_hdr + 12, 92);
    put_le32(primary_hdr + 16, 0);        /* header crc (later) */
    put_le32(primary_hdr + 20, 0);
    put_le64(primary_hdr + 24, 1);                        /* current */
    put_le64(primary_hdr + 32, total_sectors - 1);        /* backup  */
    put_le64(primary_hdr + 40, first_usable);
    put_le64(primary_hdr + 48, last_usable);
    memcpy(primary_hdr + 56, disk_guid, 16);
    put_le64(primary_hdr + 72, 2);                        /* entries lba */
    put_le32(primary_hdr + 80, RK_GPT_ENTRIES);
    put_le32(primary_hdr + 84, RK_GPT_ENTRY_SIZE);
    put_le32(primary_hdr + 88, ent_crc);
    uint32_t h1 = crc32_ieee(0, primary_hdr, 92);
    put_le32(primary_hdr + 16, h1);

    /* Backup header at LBA last */
    memcpy(backup_hdr + 0, "EFI PART", 8);
    put_le32(backup_hdr + 8, 0x00010000);
    put_le32(backup_hdr + 12, 92);
    put_le32(backup_hdr + 20, 0);
    put_le64(backup_hdr + 24, total_sectors - 1);
    put_le64(backup_hdr + 32, 1);
    put_le64(backup_hdr + 40, first_usable);
    put_le64(backup_hdr + 48, last_usable);
    memcpy(backup_hdr + 56, disk_guid, 16);
    put_le64(backup_hdr + 72, total_sectors - 33);
    put_le32(backup_hdr + 80, RK_GPT_ENTRIES);
    put_le32(backup_hdr + 84, RK_GPT_ENTRY_SIZE);
    put_le32(backup_hdr + 88, ent_crc);
    put_le32(backup_hdr + 16, 0);
    uint32_t h2 = crc32_ieee(0, backup_hdr, 92);
    put_le32(backup_hdr + 16, h2);

    memcpy(backup_entries, primary_entries, ent_bytes);
    return 0;
}
