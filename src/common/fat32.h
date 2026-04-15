#ifndef RK_FAT32_H
#define RK_FAT32_H

#include "rk_types.h"

struct rk_disk;

/*
 * Create a fresh FAT32 filesystem inside a partition of `size_lba` 512-byte
 * sectors starting at `start_lba`. The volume label is copied verbatim (max
 * 11 chars, padded with spaces).
 */
int rk_fat32_format(struct rk_disk *d, uint64_t start_lba, uint64_t size_lba,
                    const char *label);

/*
 * Copy a host file into the root directory of a FAT32 volume created with
 * rk_fat32_format() above. The `name83` argument is the 8.3 short name to
 * store (e.g. "UPDATE  IMG"). For names that don't fit 8.3, pass the desired
 * short form; long names are not generated in this first implementation.
 */
int rk_fat32_add_file(struct rk_disk *d, uint64_t start_lba,
                      const char *src_path, const char *name83);

#endif
