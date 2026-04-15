#include "cli.h"
#include "../common/rkimg.h"
#include "../common/util.h"

#include <stdio.h>
#include <string.h>

int cmd_info(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: rk_image_tool info <update.img>\n");
        return 1;
    }
    struct rk_image img;
    if (rk_image_open(&img, argv[1]) != 0) return 1;

    if (img.has_rkfw) {
        printf("RKFW header\n");
        printf("  version      : %u.%u.%u\n",
               (img.rkfw.version >> 24) & 0xFF,
               (img.rkfw.version >> 16) & 0xFF,
                img.rkfw.version & 0xFFFF);
        printf("  build time   : %u-%02u-%02u %02u:%02u:%02u\n",
               img.rkfw.year, img.rkfw.month, img.rkfw.day,
               img.rkfw.hour, img.rkfw.minute, img.rkfw.second);
        printf("  chip code    : 0x%x\n", img.rkfw.chip);
        printf("  loader offset: 0x%x\n", img.rkfw.loader_offset);
        printf("  loader length: %u\n",    img.rkfw.loader_length);
        printf("  image offset : 0x%x\n", img.rkfw.image_offset);
        printf("  image length : %u\n",    img.rkfw.image_length);
        printf("  backup end   : 0x%x\n", img.rkfw.backup_endpos);
    }
    printf("RKAF header\n");
    printf("  model        : %.34s\n", img.rkaf.model);
    printf("  id           : %.30s\n", img.rkaf.id);
    printf("  manufacturer : %.56s\n", img.rkaf.manufacturer);
    printf("  version      : %08x\n", img.rkaf.version);
    printf("  num_parts    : %u\n", img.rkaf.num_parts);

    uint32_t n = img.rkaf.num_parts > 16 ? 16 : img.rkaf.num_parts;
    printf("  partitions:\n");
    printf("    %-20s %-20s %-10s %-10s %-10s\n",
           "name", "file", "size", "pos", "nand_addr");
    for (uint32_t i = 0; i < n; ++i) {
        const struct rkaf_part *p = &img.rkaf.parts[i];
        printf("    %-20.32s %-20.60s 0x%-8x 0x%-8x 0x%-8x\n",
               p->name, p->filename, p->size, p->pos, p->nand_addr);
    }

    if (img.has_rkfw) {
        printf("md5 check    : %s\n",
               rk_image_verify_md5(&img) == 0 ? "OK" : "FAILED");
    }
    rk_image_close(&img);
    return 0;
}
