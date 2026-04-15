#ifndef RK_MBR_H
#define RK_MBR_H

#include "rk_types.h"

/*
 * Build a protective MBR for a GPT-partitioned SD card.
 *
 * The 512-byte MBR has a single partition entry of type 0xEE (GPT protective)
 * covering the entire disk, or 0xFFFFFFFF if disk > 2 TiB.
 */
void rk_mbr_build_protective(uint8_t mbr[512], uint64_t total_sectors);

/*
 * Build a plain MBR with a single FAT32 partition covering (first_lba .. end).
 * Used by the "Restore" operation to turn a Rockchip SD card back into a
 * normal removable disk.
 */
void rk_mbr_build_fat32(uint8_t mbr[512], uint64_t total_sectors,
                        uint32_t first_lba);

#endif
