#include "cli.h"
#include "../common/rkpack.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("usage: rk_image_tool pack [options]\n"
           "  --src <dir>           packed Image/ tree (parameter + package-file)\n"
           "  --image <file>        re-pack an existing update.img (instead of --src)\n"
           "  --loader <file>       MiniLoader, required for --rkfw\n"
           "  --out <file>          output .img or .img.xz (required)\n"
           "  --rkfw                wrap RKAF in an RKFW rom.img (needs --loader)\n"
           "  --xz <0-9>            xz compression preset when out ends with .xz (default 6)\n"
           "  --chip <hex>          chip id word for RKFW header (default 0x33333033)\n"
           "  --rom-version <hex>   rom version for RKFW header (default 0x01000000)\n");
}

static uint32_t parse_u32(const char *s)
{
    return (uint32_t)strtoul(s, NULL, 0);
}

int cmd_pack(int argc, char **argv)
{
    struct rk_pack_params p;
    memset(&p, 0, sizeof(p));
    p.xz_preset = 6;
    p.chip = 0x33333033u;
    p.rom_version = 0x01000000u;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--src") && i + 1 < argc)        p.src_dir = argv[++i];
        else if (!strcmp(argv[i], "--image") && i + 1 < argc) p.update_img_path = argv[++i];
        else if (!strcmp(argv[i], "--loader") && i + 1 < argc) p.loader_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)   p.out_path = argv[++i];
        else if (!strcmp(argv[i], "--rkfw"))                  p.write_rkfw = 1;
        else if (!strcmp(argv[i], "--xz") && i + 1 < argc)    p.xz_preset = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--chip") && i + 1 < argc)  p.chip = parse_u32(argv[++i]);
        else if (!strcmp(argv[i], "--rom-version") && i + 1 < argc)
            p.rom_version = parse_u32(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }

    if (!p.out_path) { usage(); return 1; }
    if (!p.src_dir && !p.update_img_path) {
        rk_err("specify either --src or --image\n");
        usage();
        return 1;
    }
    if (p.src_dir && p.update_img_path) {
        rk_err("--src and --image are mutually exclusive\n");
        return 1;
    }
    if (p.write_rkfw && !p.loader_path) {
        rk_err("--rkfw requires --loader\n");
        return 1;
    }

    if (p.src_dir) return rk_pack_from_dir(&p);
    return rk_pack_from_image(&p);
}
