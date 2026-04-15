#include "cli.h"
#include "../common/sdcard.h"
#include "../common/disk.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("usage: rk_image_tool upgrade [options]\n"
           "  --image <file>        update.img (required)\n"
           "  --sdboot <file>       SDBoot.bin (required unless --use-fw-loader)\n"
           "  --device <path>       target disk\n"
           "  --image-out <file>    write to an image file\n"
           "  --size-gb <n>         size for --image-out (default 16)\n"
           "  --demo <path>         optional Demo file to place on user disk\n"
           "  --boot-config <path>  sd_boot_config.config template to copy\n"
           "  --label <name>        FAT32 volume label (default: RK_UPDATE)\n"
           "  --use-fw-loader       use MiniLoader from update.img\n"
           "  --no-format           skip FAT32 format of user disk\n"
           "  --dry-run             do not write, just compute and print\n");
}

int cmd_upgrade(int argc, char **argv)
{
    struct rk_sd_params p = { .mode = RK_SD_MODE_UPGRADE, .userdisk_label = "RK_UPDATE" };
    const char *device = NULL, *image_out = NULL;
    uint64_t image_out_size = 16ULL * 1024 * 1024 * 1024;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--image") && i+1 < argc)       p.update_img_path = argv[++i];
        else if (!strcmp(argv[i], "--sdboot") && i+1 < argc) p.sdboot_bin_path = argv[++i];
        else if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--image-out") && i+1 < argc) image_out = argv[++i];
        else if (!strcmp(argv[i], "--size-gb") && i+1 < argc) image_out_size = strtoull(argv[++i], NULL, 10) * 1024ULL*1024*1024;
        else if (!strcmp(argv[i], "--demo") && i+1 < argc) p.demo_path = argv[++i];
        else if (!strcmp(argv[i], "--boot-config") && i+1 < argc) p.sd_boot_config = argv[++i];
        else if (!strcmp(argv[i], "--label") && i+1 < argc) p.userdisk_label = argv[++i];
        else if (!strcmp(argv[i], "--use-fw-loader")) p.use_fw_loader = 1;
        else if (!strcmp(argv[i], "--no-format")) p.skip_userdisk_format = 1;
        else if (!strcmp(argv[i], "--dry-run")) p.no_write = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (!p.update_img_path || (!device && !image_out) ||
        (!p.sdboot_bin_path && !p.use_fw_loader)) { usage(); return 1; }

    struct rk_disk *d = image_out
        ? rk_disk_open_image(image_out, image_out_size)
        : rk_disk_open(device);
    if (!d) return 1;
    int rc = rk_sd_create(d, &p);
    rk_disk_close(d);
    return rc;
}
