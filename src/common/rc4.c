#include "rc4.h"

const uint8_t RK_RC4_KEY[16] = {
    0x7C, 0x4E, 0x03, 0x04, 0x55, 0x05, 0x09, 0x07,
    0x2D, 0x2C, 0x7B, 0x38, 0x17, 0x0D, 0x17, 0x11
};

void rk_rc4(const uint8_t *key, size_t keylen, uint8_t *buf, size_t len)
{
    uint8_t S[256];
    for (int i = 0; i < 256; ++i) S[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (uint8_t)(j + S[i] + key[i % keylen]);
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
    uint8_t a = 0, b = 0;
    for (size_t n = 0; n < len; ++n) {
        a++;
        b = (uint8_t)(b + S[a]);
        uint8_t t = S[a]; S[a] = S[b]; S[b] = t;
        buf[n] ^= S[(uint8_t)(S[a] + S[b])];
    }
}
