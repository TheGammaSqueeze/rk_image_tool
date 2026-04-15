#include "fat32.h"
#include "sink.h"
#include "progress.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RK_SECTOR 512u

static void le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void le32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static uint32_t pick_cluster_sectors(uint64_t size_lba)
{
    uint64_t mb = size_lba / 2048;
    if (mb <= 260)       return 1;
    if (mb <= 8192)      return 8;
    if (mb <= 16384)     return 16;
    if (mb <= 32768)     return 32;
    return 64;
}

static uint32_t fat_date(void)
{
    time_t t = time(NULL);
    struct tm tm_copy;
    struct tm *lt = localtime(&t);
    if (!lt) return 0;
    tm_copy = *lt;
    int year = tm_copy.tm_year + 1900 - 1980;
    if (year < 0) year = 0;
    return (uint32_t)(((year & 0x7F) << 9) | (((tm_copy.tm_mon+1) & 0xF) << 5) |
                     (tm_copy.tm_mday & 0x1F));
}

static uint32_t fat_time(void)
{
    time_t t = time(NULL);
    struct tm tm_copy;
    struct tm *lt = localtime(&t);
    if (!lt) return 0;
    tm_copy = *lt;
    return (uint32_t)(((tm_copy.tm_hour & 0x1F) << 11) | ((tm_copy.tm_min & 0x3F) << 5) |
                     ((tm_copy.tm_sec/2) & 0x1F));
}

int rk_fat32_format(struct rk_sink *s, struct rk_fat32_state *st,
                    uint64_t start_lba, uint64_t size_lba, const char *label)
{
    if (size_lba < 66560) {
        rk_err("partition too small for FAT32 (%llu sectors)\n",
               (unsigned long long)size_lba);
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->start_lba = start_lba;
    st->partition_size_lba = size_lba;
    st->spc = pick_cluster_sectors(size_lba);
    st->reserved = 32;
    st->root_cluster = 2;
    st->cluster_bytes = (uint64_t)st->spc * RK_SECTOR;

    uint64_t data_sectors = size_lba - st->reserved;
    uint64_t total_clusters = data_sectors / st->spc;
    uint64_t fat_bytes = (total_clusters + 2) * 4;
    st->fat_sectors = (uint32_t)((fat_bytes + RK_SECTOR - 1) / RK_SECTOR);
    data_sectors = size_lba - st->reserved - 2ULL * st->fat_sectors;
    total_clusters = data_sectors / st->spc;

    st->fat1_off = (start_lba + st->reserved) * RK_SECTOR;
    st->fat2_off = st->fat1_off + (uint64_t)st->fat_sectors * RK_SECTOR;
    st->data_off = (start_lba + st->reserved + 2ULL * st->fat_sectors) * RK_SECTOR;
    st->next_cluster = 3;
    st->root_dir_slots_used = 1;  /* slot 0 is the volume label */
    st->root_dir_slots_max = (uint32_t)(st->cluster_bytes / 32);

    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0]=0xEB; bs[1]=0x58; bs[2]=0x90;
    memcpy(bs + 3, "MSWIN4.1", 8);
    le16(bs + 11, RK_SECTOR);
    bs[13] = (uint8_t)st->spc;
    le16(bs + 14, (uint16_t)st->reserved);
    bs[16] = 2;
    le16(bs + 17, 0);
    le16(bs + 19, 0);
    bs[21] = 0xF8;
    le16(bs + 22, 0);
    le16(bs + 24, 63);
    le16(bs + 26, 255);
    le32(bs + 28, (uint32_t)start_lba);
    le32(bs + 32, size_lba > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)size_lba);
    le32(bs + 36, st->fat_sectors);
    le16(bs + 40, 0);
    le16(bs + 42, 0);
    le32(bs + 44, st->root_cluster);
    le16(bs + 48, 1);
    le16(bs + 50, 6);
    bs[66] = 0x80;
    bs[67] = 0;
    bs[68] = 0x29;
    le32(bs + 69, 0x12345678);
    uint8_t lbl[11];
    memset(lbl, ' ', 11);
    if (label) for (int i = 0; i < 11 && label[i]; ++i) {
        uint8_t c = (uint8_t)label[i];
        lbl[i] = (c >= 'a' && c <= 'z') ? (uint8_t)(c - 0x20) : c;
    }
    memcpy(bs + 71, lbl, 11);
    memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    uint8_t fsi[512];
    memset(fsi, 0, sizeof(fsi));
    le32(fsi + 0, 0x41615252);
    le32(fsi + 484, 0x61417272);
    le32(fsi + 488, (uint32_t)(total_clusters - 1));
    le32(fsi + 492, 3);
    le16(fsi + 510, 0xAA55);

    uint64_t base = start_lba * RK_SECTOR;
    if (rk_sink_write(s, base + 0 * RK_SECTOR, bs, 512) != 0) return -1;
    if (rk_sink_write(s, base + 1 * RK_SECTOR, fsi, 512) != 0) return -1;
    if (rk_sink_write(s, base + 6 * RK_SECTOR, bs, 512) != 0) return -1;
    if (rk_sink_write(s, base + 7 * RK_SECTOR, fsi, 512) != 0) return -1;

    if (rk_sink_zero(s, st->fat1_off,
                     (uint64_t)st->fat_sectors * 2 * RK_SECTOR) != 0) return -1;

    uint8_t fat_start[12];
    memset(fat_start, 0, sizeof(fat_start));
    le32(fat_start + 0, 0x0FFFFFF8);
    le32(fat_start + 4, 0x0FFFFFFF);
    le32(fat_start + 8, 0x0FFFFFFF);
    if (rk_sink_write(s, st->fat1_off, fat_start, sizeof(fat_start)) != 0) return -1;
    if (rk_sink_write(s, st->fat2_off, fat_start, sizeof(fat_start)) != 0) return -1;

    uint64_t root_off = st->data_off + (st->root_cluster - 2) * st->cluster_bytes;
    if (rk_sink_zero(s, root_off, st->cluster_bytes) != 0) return -1;

    uint8_t vol_entry[32];
    memset(vol_entry, 0, sizeof(vol_entry));
    memcpy(vol_entry, lbl, 11);
    vol_entry[11] = 0x08;
    uint16_t fd = (uint16_t)fat_date();
    uint16_t ft = (uint16_t)fat_time();
    le16(vol_entry + 16, fd);
    le16(vol_entry + 22, ft);
    le16(vol_entry + 24, fd);
    if (rk_sink_write(s, root_off, vol_entry, sizeof(vol_entry)) != 0) return -1;

    return 0;
}

static uint8_t lfn_checksum(const uint8_t *name83)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; ++i)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + name83[i]);
    return sum;
}

static int allowed_83_char(int c)
{
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (strchr("$%'-_@~`!(){}^#&", c)) return 1;
    return 0;
}

static void derive_83_alias(const char *long_name, uint32_t ordinal,
                            uint8_t out[11])
{
    memset(out, ' ', 11);
    /* split into base and extension at the LAST dot */
    const char *dot = strrchr(long_name, '.');
    const char *base_end = dot ? dot : long_name + strlen(long_name);
    const char *ext_start = dot ? dot + 1 : NULL;

    /* Write up to 6 chars of base, uppercased, skipping invalid characters. */
    int pos = 0;
    for (const char *p = long_name; p < base_end && pos < 6; ++p) {
        int c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        if (c == ' ' || c == '.') continue;
        if (!allowed_83_char(c)) c = '_';
        out[pos++] = (uint8_t)c;
    }

    /* Append ~N */
    char num[8];
    snprintf(num, sizeof(num), "~%u", (unsigned)ordinal);
    int nlen = (int)strlen(num);
    int start = pos <= (8 - nlen) ? pos : (8 - nlen);
    if (start < 0) start = 0;
    for (int i = 0; i < nlen && (start + i) < 8; ++i)
        out[start + i] = (uint8_t)num[i];

    /* Extension (up to 3 chars) */
    if (ext_start) {
        int epos = 8;
        for (const char *p = ext_start; *p && epos < 11; ++p) {
            int c = (unsigned char)*p;
            if (c >= 'a' && c <= 'z') c -= 0x20;
            if (!allowed_83_char(c)) c = '_';
            out[epos++] = (uint8_t)c;
        }
    }
}

static int write_lfn_entries(struct rk_sink *s, struct rk_fat32_state *st,
                             uint64_t entry0_off,
                             const char *long_name, uint8_t csum)
{
    size_t nlen = strlen(long_name);
    uint16_t utf16[260 + 1];
    size_t nchars = 0;
    for (size_t i = 0; i < nlen && nchars < 260; ++i) {
        utf16[nchars++] = (uint16_t)(unsigned char)long_name[i];
    }
    utf16[nchars] = 0x0000;
    nchars++;
    while (nchars % 13) utf16[nchars++] = 0xFFFF;

    size_t entries = nchars / 13;
    if (entries > 20) return -1;
    uint64_t room = st->cluster_bytes - (entry0_off - (st->data_off +
        (st->root_cluster - 2) * st->cluster_bytes));
    if (room < (entries + 1) * 32ULL) {
        rk_err("root directory full, cannot fit LFN for '%s'\n", long_name);
        return -1;
    }

    for (size_t i = 0; i < entries; ++i) {
        uint8_t e[32];
        memset(e, 0, sizeof(e));
        size_t ord = entries - i;
        e[0] = (uint8_t)ord;
        if (ord == entries) e[0] |= 0x40;
        e[11] = 0x0F;
        e[12] = 0;
        e[13] = csum;
        const uint16_t *chunk = utf16 + (ord - 1) * 13;
        static const int offs[13] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};
        for (int j = 0; j < 13; ++j) {
            e[offs[j]    ] = (uint8_t)(chunk[j] & 0xFF);
            e[offs[j] + 1] = (uint8_t)(chunk[j] >> 8);
        }
        if (rk_sink_write(s, entry0_off + (uint64_t)i * 32, e, 32) != 0) return -1;
    }
    return (int)entries;
}

static int fat_set_entry(struct rk_sink *s, struct rk_fat32_state *st,
                         uint32_t cluster, uint32_t value)
{
    uint8_t b[4];
    le32(b, value);
    if (rk_sink_write(s, st->fat1_off + (uint64_t)cluster * 4, b, 4) != 0) return -1;
    if (rk_sink_write(s, st->fat2_off + (uint64_t)cluster * 4, b, 4) != 0) return -1;
    return 0;
}

int rk_fat32_add_file_progress(struct rk_sink *s, struct rk_fat32_state *st,
                               const char *src_path, const char *long_name,
                               struct rk_progress *pg)
{
    if (!long_name || !*long_name) long_name = src_path;
    const char *slash = strrchr(long_name, '/');
    if (slash) long_name = slash + 1;

    FILE *fp = fopen(src_path, "rb");
    if (!fp) { rk_err("open %s\n", src_path); return -1; }
    uint64_t file_size = rk_file_size(fp);
    fclose(fp);

    uint32_t clusters_needed = file_size == 0 ? 0 :
        (uint32_t)((file_size + st->cluster_bytes - 1) / st->cluster_bytes);

    uint32_t first_cluster = clusters_needed ? st->next_cluster : 0;
    for (uint32_t i = 0; i < clusters_needed; ++i) {
        uint32_t cur = st->next_cluster + i;
        uint32_t next = (i == clusters_needed - 1) ? 0x0FFFFFFFu : (cur + 1);
        if (fat_set_entry(s, st, cur, next) != 0) return -1;
    }

    if (clusters_needed) {
        uint64_t data_start = st->data_off +
            (uint64_t)(first_cluster - 2) * st->cluster_bytes;
        uint64_t tail_zero = (uint64_t)clusters_needed * st->cluster_bytes - file_size;
        if (rk_sink_write_file_progress(s, data_start, src_path, 0, file_size, pg) != 0)
            return -1;
        if (tail_zero && rk_sink_zero(s, data_start + file_size, tail_zero) != 0)
            return -1;
        st->next_cluster += clusters_needed;
    }

    uint8_t name83[11];
    derive_83_alias(long_name, 1, name83);
    uint8_t csum = lfn_checksum(name83);

    uint64_t root_off = st->data_off + (st->root_cluster - 2) * st->cluster_bytes;
    uint64_t slot_off = root_off + (uint64_t)st->root_dir_slots_used * 32;

    int n_lfn = write_lfn_entries(s, st, slot_off, long_name, csum);
    if (n_lfn < 0) return -1;

    uint8_t entry[32];
    memset(entry, 0, sizeof(entry));
    memcpy(entry, name83, 11);
    entry[11] = 0x20;
    uint16_t fd = (uint16_t)fat_date();
    uint16_t ft = (uint16_t)fat_time();
    le16(entry + 14, ft);
    le16(entry + 16, fd);
    le16(entry + 18, fd);
    le16(entry + 22, ft);
    le16(entry + 24, fd);
    le16(entry + 26, (uint16_t)(first_cluster & 0xFFFF));
    le16(entry + 20, (uint16_t)(first_cluster >> 16));
    le32(entry + 28, (uint32_t)file_size);

    uint64_t entry_off = slot_off + (uint64_t)n_lfn * 32;
    if (rk_sink_write(s, entry_off, entry, sizeof(entry)) != 0) return -1;

    st->root_dir_slots_used += (uint32_t)n_lfn + 1;
    return 0;
}

int rk_fat32_add_file(struct rk_sink *s, struct rk_fat32_state *st,
                      const char *src_path, const char *long_name)
{
    return rk_fat32_add_file_progress(s, st, src_path, long_name, NULL);
}
