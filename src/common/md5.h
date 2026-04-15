#ifndef RK_MD5_H
#define RK_MD5_H

#include <stdint.h>
#include <stddef.h>

struct md5_ctx {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[64];
};

void md5_init(struct md5_ctx *ctx);
void md5_update(struct md5_ctx *ctx, const void *data, size_t len);
void md5_final(struct md5_ctx *ctx, uint8_t digest[16]);
void md5_hex(const uint8_t digest[16], char out[33]);

#endif
