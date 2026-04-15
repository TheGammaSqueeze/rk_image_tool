#ifndef RK_MISC_CMD_H
#define RK_MISC_CMD_H

#include "rk_types.h"

/*
 * Produce an 8 KiB misc block matching SD_Firmware_Tool v1.69.
 *
 * The tool's `WriteMiscItem` builds a 0x2000-byte buffer where the first
 * 0x800 bytes are zero and the Android `bootloader_message` struct starts at
 * offset 0x800:
 *     +0x800 "boot-recovery\0...\0"     (command, 32 bytes)
 *     +0x820 zero                        (status, 32 bytes)
 *     +0x840 "recovery\n--rk_fwupdate\n\0..."  (recovery args, 768 bytes)
 *     +0xB40..0x1FFF  zero
 *
 * The recovery partition's on-device bootloader reads this on boot, sees the
 * rk_fwupdate command, mounts the SD card, and re-flashes itself from
 * `/sdupdate.img` on the FAT32 userdata volume.
 *
 * buf must be at least 8192 bytes.
 */
int rk_misc_build_fwupdate(uint8_t *buf, size_t buf_len);

/* Zeroed misc block (used by "Restore"). */
int rk_misc_build_clean(uint8_t *buf, size_t buf_len);

#endif
