#include "cli.h"
#include "../common/sdcard.h"
#include "../common/disk.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("usage: rk_image_tool sd-boot [options]\n"
           "  --image <file>        update.img (required)\n"
           "  --sdboot <file>       SDBoot.bin (required unless --use-fw-loader)\n"
           "  --device <path>       target disk (/dev/sdX, \\\\.\\PhysicalDriveN)\n"
           "  --image-out <file>    write to an image file instead of a device\n"
           "  --size-gb <n>         size for --image-out (default 16)\n"
           "  --use-fw-loader       use MiniLoader from update.img (RK3288)\n"
           "  --dry-run             do not write, just compute and print\n");
}

int cmd_sdboot(int argc, char **argv)
{
    struct rk_sd_params p = { .mode = RK_SD_MODE_SDBOOT, .userdisk_label = "RK_SDBOOT" };
    const char *device = NULL, *image_out = NULL;
    uint64_t image_out_size = 16ULL * 1024 * 1024 * 1024;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--image") && i+1 < argc)       p.update_img_path = argv[++i];
        else if (!strcmp(argv[i], "--sdboot") && i+1 < argc) p.sdboot_bin_path = argv[++i];
        else if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--image-out") && i+1 < argc) image_out = argv[++i];
        else if (!strcmp(argv[i], "--size-gb") && i+1 < argc) image_out_size = strtoull(argv[++i], NULL, 10) * 1024ULL*1024*1024;
        else if (!strcmp(argv[i], "--use-fw-loader")) p.use_fw_loader = 1;
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
