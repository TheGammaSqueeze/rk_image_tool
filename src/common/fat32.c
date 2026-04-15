#include "fat32.h"
#include "disk.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Minimal FAT32 formatter.
 *
 * Layout inside the partition (all values are multiples of 512-byte sectors):
 *   0                          : boot sector (BPB)
 *   1                          : FS Info sector
 *   6                          : boot sector backup
 *   7                          : FS Info backup
 *   32                         : reserved sectors end, FAT #1 begins
 *   32 + fat_sectors           : FAT #2 begins
 *   32 + 2*fat_sectors         : cluster 2 (root dir) begins
 *
 * Cluster size is chosen based on partition size, matching Microsoft's table.
 */

static void le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void le32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static uint32_t pick_cluster_sectors(uint64_t size_lba)
{
    uint64_t mb = size_lba / 2048;
    if (mb <= 260)       return 1;    /* 512 B */
    if (mb <= 8192)      return 8;    /* 4 KiB */
    if (mb <= 16384)     return 16;   /* 8 KiB */
    if (mb <= 32768)     return 32;   /* 16 KiB */
    return 64;                         /* 32 KiB */
}

static uint32_t fat_date(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (!lt) return 0;
    int year = lt->tm_year + 1900 - 1980;
    if (year < 0) year = 0;
    return (uint32_t)(((year & 0x7F) << 9) | (((lt->tm_mon+1) & 0xF) << 5) | (lt->tm_mday & 0x1F));
}
static uint32_t fat_time(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (!lt) return 0;
    return (uint32_t)(((lt->tm_hour & 0x1F) << 11) | ((lt->tm_min & 0x3F) << 5) | ((lt->tm_sec/2) & 0x1F));
}

int rk_fat32_format(struct rk_disk *d, uint64_t start_lba, uint64_t size_lba,
                    const char *label)
{
    if (size_lba < 66560) {   /* <~32 MiB is too small for FAT32 */
        rk_err("partition too small for FAT32 (%llu sectors)\n",
               (unsigned long long)size_lba);
        return -1;
    }
    uint32_t spc = pick_cluster_sectors(size_lba);
    uint32_t reserved = 32;

    /* approximate FAT size, then iterate to converge */
    uint64_t data_sectors = size_lba - reserved;
    uint64_t total_clusters = data_sectors / spc;
    uint64_t fat_bytes = (total_clusters + 2) * 4;
    uint32_t fat_sectors = (uint32_t)((fat_bytes + 511) / 512);
    data_sectors = size_lba - reserved - 2 * fat_sectors;
    total_clusters = data_sectors / spc;

    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0]=0xEB; bs[1]=0x58; bs[2]=0x90;            /* jmp */
    memcpy(bs + 3, "MSWIN4.1", 8);
    le16(bs + 11, 512);                             /* bytes per sector */
    bs[13] = (uint8_t)spc;                          /* sectors per cluster */
    le16(bs + 14, (uint16_t)reserved);
    bs[16] = 2;                                     /* # FATs */
    le16(bs + 17, 0);                               /* root entries (unused) */
    le16(bs + 19, 0);                               /* total sectors 16 */
    bs[21] = 0xF8;                                  /* media */
    le16(bs + 22, 0);                               /* FAT size 16 (unused) */
    le16(bs + 24, 63);                              /* sectors per track */
    le16(bs + 26, 255);                             /* heads */
    le32(bs + 28, (uint32_t)start_lba);             /* hidden sectors */
    le32(bs + 32, size_lba > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)size_lba);
    le32(bs + 36, fat_sectors);                     /* FAT size 32 */
    le16(bs + 40, 0);                               /* ext flags */
    le16(bs + 42, 0);                               /* fs version */
    le32(bs + 44, 2);                               /* root cluster */
    le16(bs + 48, 1);                               /* fsinfo sector */
    le16(bs + 50, 6);                               /* backup boot sector */
    bs[66] = 0x80;                                  /* drive number */
    bs[67] = 0;
    bs[68] = 0x29;                                  /* ext sig */
    le32(bs + 69, 0x12345678);                      /* volume id */
    char lbl[11];
    memset(lbl, ' ', 11);
    if (label) for (int i = 0; i < 11 && label[i]; ++i) lbl[i] = label[i];
    memcpy(bs + 71, lbl, 11);
    memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    uint8_t fsi[512];
    memset(fsi, 0, sizeof(fsi));
    le32(fsi + 0, 0x41615252);
    le32(fsi + 484, 0x61417272);
    le32(fsi + 488, (uint32_t)(total_clusters - 1));  /* free clusters */
    le32(fsi + 492, 3);                               /* next free */
    le16(fsi + 510, 0xAA55);

    if (rk_disk_write(d, start_lba * RK_SECTOR_SIZE, bs, 512) != 0) return -1;
    if (rk_disk_write(d, (start_lba + 1) * RK_SECTOR_SIZE, fsi, 512) != 0) return -1;
    if (rk_disk_write(d, (start_lba + 6) * RK_SECTOR_SIZE, bs, 512) != 0) return -1;
    if (rk_disk_write(d, (start_lba + 7) * RK_SECTOR_SIZE, fsi, 512) != 0) return -1;

    if (rk_disk_zero(d, (start_lba + reserved) * RK_SECTOR_SIZE,
                     (uint64_t)fat_sectors * 2 * RK_SECTOR_SIZE) != 0)
        return -1;

    uint8_t fat_start[12];
    memset(fat_start, 0, sizeof(fat_start));
    le32(fat_start + 0, 0x0FFFFFF8);   /* media descriptor */
    le32(fat_start + 4, 0x0FFFFFFF);   /* end-of-chain marker */
    le32(fat_start + 8, 0x0FFFFFFF);   /* cluster 2 (root) EOC */

    uint64_t fat1_off = (start_lba + reserved) * RK_SECTOR_SIZE;
    uint64_t fat2_off = fat1_off + (uint64_t)fat_sectors * RK_SECTOR_SIZE;
    if (rk_disk_write(d, fat1_off, fat_start, sizeof(fat_start)) != 0) return -1;
    if (rk_disk_write(d, fat2_off, fat_start, sizeof(fat_start)) != 0) return -1;

    /* Zero root cluster and write volume label entry */
    uint64_t root_off = (start_lba + reserved + 2ULL * fat_sectors) * RK_SECTOR_SIZE;
    if (rk_disk_zero(d, root_off, (uint64_t)spc * RK_SECTOR_SIZE) != 0) return -1;

    uint8_t vol_entry[32];
    memset(vol_entry, 0, sizeof(vol_entry));
    memcpy(vol_entry, lbl, 11);
    vol_entry[11] = 0x08;                            /* volume label */
    uint16_t fd = (uint16_t)fat_date();
    uint16_t ft = (uint16_t)fat_time();
    le16(vol_entry + 16, fd);
    le16(vol_entry + 22, ft);
    le16(vol_entry + 24, fd);
    if (rk_disk_write(d, root_off, vol_entry, sizeof(vol_entry)) != 0) return -1;

    return 0;
}

static int read_le32(struct rk_disk *d, uint64_t off, uint32_t *v)
{
    uint8_t b[4];
    if (rk_disk_read(d, off, b, 4) != 0) return -1;
    *v = (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    return 0;
}
static int read_le16(struct rk_disk *d, uint64_t off, uint16_t *v)
{
    uint8_t b[2];
    if (rk_disk_read(d, off, b, 2) != 0) return -1;
    *v = (uint16_t)b[0] | ((uint16_t)b[1]<<8);
    return 0;
}

int rk_fat32_add_file(struct rk_disk *d, uint64_t start_lba,
                      const char *src_path, const char *name83)
{
    uint16_t sect_sz, reserved_u16;
    uint8_t spc_b;
    uint32_t fat_size, root_cluster;
    if (read_le16(d, start_lba * RK_SECTOR_SIZE + 11, &sect_sz) != 0) return -1;
    if (rk_disk_read(d, start_lba * RK_SECTOR_SIZE + 13, &spc_b, 1) != 0) return -1;
    if (read_le16(d, start_lba * RK_SECTOR_SIZE + 14, &reserved_u16) != 0) return -1;
    if (read_le32(d, start_lba * RK_SECTOR_SIZE + 36, &fat_size) != 0) return -1;
    if (read_le32(d, start_lba * RK_SECTOR_SIZE + 44, &root_cluster) != 0) return -1;

    uint32_t reserved = reserved_u16;
    uint32_t spc = spc_b;
    uint64_t cluster_bytes = (uint64_t)spc * sect_sz;
    uint64_t fat1_off = (start_lba + reserved) * RK_SECTOR_SIZE;
    uint64_t fat2_off = fat1_off + (uint64_t)fat_size * RK_SECTOR_SIZE;
    uint64_t data_off = (start_lba + reserved + 2ULL * fat_size) * RK_SECTOR_SIZE;

    FILE *fp = fopen(src_path, "rb");
    if (!fp) return -1;
    uint64_t file_size = rk_file_size(fp);
    uint32_t clusters_needed = (uint32_t)((file_size + cluster_bytes - 1) / cluster_bytes);
    if (clusters_needed == 0) clusters_needed = 1;

    uint32_t first_data_cluster = 3;
    uint32_t cur = first_data_cluster;
    for (uint32_t i = 0; i < clusters_needed; ++i, ++cur) {
        uint32_t next = (i == clusters_needed - 1) ? 0x0FFFFFFF : (cur + 1);
        uint8_t b[4];
        le32(b, next);
        if (rk_disk_write(d, fat1_off + (uint64_t)cur * 4, b, 4) != 0) { fclose(fp); return -1; }
        if (rk_disk_write(d, fat2_off + (uint64_t)cur * 4, b, 4) != 0) { fclose(fp); return -1; }
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)cluster_bytes);
    if (!buf) { fclose(fp); return -1; }

    for (uint32_t i = 0; i < clusters_needed; ++i) {
        memset(buf, 0, (size_t)cluster_bytes);
        size_t got = fread(buf, 1, (size_t)cluster_bytes, fp);
        if (got == 0 && i != clusters_needed - 1) { free(buf); fclose(fp); return -1; }
        uint64_t cluster_num = first_data_cluster + i;
        uint64_t off = data_off + (cluster_num - 2) * cluster_bytes;
        if (rk_disk_write(d, off, buf, (size_t)cluster_bytes) != 0) {
            free(buf); fclose(fp); return -1;
        }
    }
    free(buf);
    fclose(fp);

    /* Write directory entry in root cluster after the volume label. */
    uint64_t root_off = data_off + (root_cluster - 2) * cluster_bytes;
    uint8_t probe[32];
    uint64_t entry_off = 0;
    for (uint32_t i = 0; i < cluster_bytes / 32; ++i) {
        if (rk_disk_read(d, root_off + (uint64_t)i * 32, probe, 32) != 0) return -1;
        if (probe[0] == 0 || probe[0] == 0xE5) {
            entry_off = root_off + (uint64_t)i * 32;
            break;
        }
    }
    if (entry_off == 0) return -1;

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
    le16(entry + 26, (uint16_t)(first_data_cluster & 0xFFFF));
    le16(entry + 20, (uint16_t)(first_data_cluster >> 16));
    le32(entry + 28, (uint32_t)file_size);
    if (rk_disk_write(d, entry_off, entry, sizeof(entry)) != 0) return -1;

    return 0;
}
