#ifndef RK_CRC_H
#define RK_CRC_H

#include <stdint.h>
#include <stddef.h>

/* Rockchip "RKCRC" used to CRC parameter, kernel, boot partitions. */
uint32_t rk_crc32(uint32_t crc, const void *buf, size_t size);

/* Classic CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) used by GPT. */
uint32_t crc32_ieee(uint32_t crc, const void *buf, size_t size);

/* Rockchip IDBlock CRC16 (polynomial 0x1021, init 0xFFFF). Stored big-endian
 * at the end of each 512-byte sector as 2-byte suffix of a 508+4 layout. */
uint16_t rk_crc16(const void *buf, size_t size);

#endif
