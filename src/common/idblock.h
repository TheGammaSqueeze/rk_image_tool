#ifndef RK_IDBLOCK_H
#define RK_IDBLOCK_H

#include "rk_types.h"

/*
 * Build a Rockchip IDBlock from a raw bootloader (SDBoot.bin / FW loader).
 *
 * Each 512-byte sector of the loader is independently RC4-encoded with the
 * fixed Rockchip key. The BootROM reads sector 64 of the SD card, decrypts
 * it with the same key, and uses the embedded FlashHead/FlashData/FlashBoot
 * layout to locate DDR-init code and the SPL.
 *
 * SD_Firmware_Tool writes a single IDB copy starting at sector 64. We follow
 * the same convention.
 *
 * out_len receives the total size (in bytes, multiple of 512).
 */
int rk_idb_build(const uint8_t *loader, uint64_t loader_len,
                 uint8_t **out, uint64_t *out_len);

/* Write the supplied raw loader as an IDB at sector 64 via a caller-supplied
 * write callback. */
typedef int (*rk_stream_write_fn)(void *user, uint64_t offset,
                                  const void *buf, size_t len);

int rk_idb_write(const uint8_t *loader, uint64_t loader_len,
                 rk_stream_write_fn write_fn, void *user);

#endif
