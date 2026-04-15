#ifndef RK_FAT32_H
#define RK_FAT32_H

#include "rk_types.h"

struct rk_sink;

struct rk_fat32_state {
    uint64_t start_lba;
    uint32_t spc;                 /* sectors per cluster */
    uint32_t reserved;            /* reserved sectors (32) */
    uint32_t fat_sectors;
    uint32_t root_cluster;
    uint64_t cluster_bytes;
    uint64_t fat1_off;
    uint64_t fat2_off;
    uint64_t data_off;            /* byte offset of cluster 2 inside the device */
    uint64_t partition_size_lba;  /* total sectors in the partition */

    uint32_t next_cluster;        /* next free data cluster */
    uint32_t root_dir_slots_used; /* number of 32-byte slots already filled in root */
    uint32_t root_dir_slots_max;  /* slots available in root (single cluster) */
};

/*
 * Create a fresh FAT32 filesystem inside a partition of `size_lba` 512-byte
 * sectors starting at `start_lba`. Populates `st` for subsequent
 * rk_fat32_add_file() calls.
 */
int rk_fat32_format(struct rk_sink *s, struct rk_fat32_state *st,
                    uint64_t start_lba, uint64_t size_lba, const char *label);

/*
 * Copy a host file into the root directory of a FAT32 volume previously
 * formatted with rk_fat32_format(). `long_name` is the display name; a
 * Microsoft-compatible 8.3 alias is generated automatically and LFN entries
 * are emitted so VFAT shows the long form.
 */
int rk_fat32_add_file(struct rk_sink *s, struct rk_fat32_state *st,
                      const char *src_path, const char *long_name);

struct rk_progress;
int rk_fat32_add_file_progress(struct rk_sink *s, struct rk_fat32_state *st,
                               const char *src_path, const char *long_name,
                               struct rk_progress *pg);

#endif
