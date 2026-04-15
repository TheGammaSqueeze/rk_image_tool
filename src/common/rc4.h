#ifndef RK_RC4_H
#define RK_RC4_H

#include <stdint.h>
#include <stddef.h>

/*
 * Rockchip IDBlock cipher: RC4 with a fixed 16-byte key.
 * The cipher is applied independently to each 512-byte sector of an IDB copy.
 * Encrypt and decrypt are identical operations.
 */
extern const uint8_t RK_RC4_KEY[16];

void rk_rc4(const uint8_t *key, size_t keylen, uint8_t *buf, size_t len);

#endif
