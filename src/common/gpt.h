#ifndef RK_GPT_H
#define RK_GPT_H

#include "rk_types.h"
#include "parameter.h"

#define RK_GPT_ENTRIES     128
#define RK_GPT_ENTRY_SIZE  128

/*
 * Build primary and backup GPT for a Rockchip SD boot/upgrade card.
 *
 * Inputs:
 *   total_sectors - size of the SD card in 512-byte sectors
 *   params        - parsed parameter.txt giving partition offsets
 *
 * Outputs (allocated by caller):
 *   primary_hdr     : 512 bytes at LBA 1
 *   primary_entries : RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE at LBA 2..33
 *   backup_hdr      : 512 bytes at LBA total_sectors - 1
 *   backup_entries  : RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE at LBA total_sectors - 33
 */
int rk_gpt_build(const struct rk_parameter *params, uint64_t total_sectors,
                 uint8_t *primary_hdr,
                 uint8_t *primary_entries,
                 uint8_t *backup_hdr,
                 uint8_t *backup_entries);

#endif
