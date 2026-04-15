#include "common/md5.h"
#include "common/rc4.h"
#include "common/crc.h"
#include "common/idblock.h"
#include "common/parameter.h"
#include "common/mbr.h"
#include "common/gpt.h"
#include "common/misc_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int fails = 0;
#define CHECK(x) do { if (!(x)) { fails++; fprintf(stderr, "FAIL: %s (line %d)\n", #x, __LINE__); } } while(0)

static void test_md5(void)
{
    struct md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, "abc", 3);
    uint8_t d[16];
    md5_final(&ctx, d);
    char hex[33];
    md5_hex(d, hex);
    CHECK(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);
}

static void test_rc4_identity(void)
{
    uint8_t data[64] = { 0 };
    for (int i = 0; i < 64; ++i) data[i] = (uint8_t)i;
    uint8_t orig[64];
    memcpy(orig, data, 64);
    rk_rc4(RK_RC4_KEY, 16, data, 64);
    CHECK(memcmp(data, orig, 64) != 0);
    rk_rc4(RK_RC4_KEY, 16, data, 64);
    CHECK(memcmp(data, orig, 64) == 0);
}

static void test_crc32_ieee(void)
{
    uint32_t c = crc32_ieee(0, "123456789", 9);
    CHECK(c == 0xCBF43926u);
}

static void test_rkcrc(void)
{
    uint32_t c = rk_crc32(0, "test", 4);
    CHECK(c != 0);
}

static void test_idb_build(void)
{
    uint8_t input[1024];
    memset(input, 'A', sizeof(input));
    uint8_t *out = NULL; uint64_t outlen = 0;
    CHECK(rk_idb_build(input, sizeof(input), &out, &outlen) == 0);
    CHECK(outlen == sizeof(input));
    /* re-decrypt each 512 block back to 'A' */
    for (uint64_t off = 0; off < outlen; off += 512)
        rk_rc4(RK_RC4_KEY, 16, out + off, 512);
    for (size_t i = 0; i < sizeof(input); ++i)
        if (out[i] != 'A') { fails++; break; }
    free(out);
}

static void test_parameter_parse(void)
{
    const char *txt =
        "FIRMWARE_VER: 1.0\n"
        "MACHINE_MODEL: test\n"
        "CMDLINE:mtdparts=rk29xxnand:0x00002000@0x00002000(uboot),"
        "0x00000800@0x0000c000(vbmeta),"
        "0x00020000@0x0000c800(boot),"
        "-@0x0001c800(userdata:grow)\n";
    struct rk_parameter p;
    CHECK(rk_parameter_parse(txt, strlen(txt), &p) == 0);
    CHECK(p.num_parts == 4);
    CHECK(strcmp(p.parts[0].name, "uboot") == 0);
    CHECK(p.parts[0].offset_lba == 0x2000);
    CHECK(p.parts[3].grow == 1);
    rk_parameter_free(&p);
}

static void test_mbr(void)
{
    uint8_t mbr[512];
    rk_mbr_build_protective(mbr, 0x01000000);
    CHECK(mbr[510] == 0x55 && mbr[511] == 0xAA);
    CHECK(mbr[0x1BE + 4] == 0xEE);
}

static void test_gpt(void)
{
    const char *txt = "CMDLINE:mtdparts=rk29xxnand:"
                      "0x00002000@0x00002000(uboot),"
                      "-@0x0000a000(userdata:grow)\n";
    struct rk_parameter p;
    rk_parameter_parse(txt, strlen(txt), &p);
    uint64_t total = 0x0E900000;   /* ~7.5 GiB of sectors */
    uint8_t phdr[512], bhdr[512];
    uint8_t *pe = calloc(1, 128*128);
    uint8_t *be = calloc(1, 128*128);
    CHECK(rk_gpt_build(&p, total, phdr, pe, bhdr, be) == 0);
    CHECK(memcmp(phdr, "EFI PART", 8) == 0);
    CHECK(memcmp(bhdr, "EFI PART", 8) == 0);
    uint32_t h1 = 0;
    memcpy(&h1, phdr + 16, 4);
    uint32_t saved = h1;
    memset(phdr + 16, 0, 4);
    CHECK(crc32_ieee(0, phdr, 92) == saved);
    free(pe); free(be);
    rk_parameter_free(&p);
}

static void test_misc(void)
{
    uint8_t buf[0x2000];
    CHECK(rk_misc_build_fwupdate(buf, sizeof(buf)) == 0);
    /* SD_Firmware_Tool places the bootloader_message struct at +0x800 */
    CHECK(memcmp(buf + 0x800, "boot-recovery", 13) == 0);
    CHECK(memcmp(buf + 0x840, "recovery\n--rk_fwupdate\n", 23) == 0);
    /* First 0x800 bytes must be zero */
    for (int i = 0; i < 0x800; ++i)
        if (buf[i] != 0) { fails++; break; }
}

int main(void)
{
    test_md5();
    test_rc4_identity();
    test_crc32_ieee();
    test_rkcrc();
    test_idb_build();
    test_parameter_parse();
    test_mbr();
    test_gpt();
    test_misc();
    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
