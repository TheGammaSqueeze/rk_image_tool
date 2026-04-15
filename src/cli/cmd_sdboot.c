#include "cli.h"
#include "../common/sdcard.h"
#include "../common/sink.h"
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
           "  --device <path>       target disk (default: entire disk)\n"
           "  --image-out <file>    write to .img or .img.xz instead of a device\n"
           "  --size-gb <n>         image size (image-out: required / device: cap)\n"
           "  --xz <0-9>            xz preset when output path ends with .xz (default 6)\n"
           "  --use-fw-loader       use MiniLoader from update.img (RK3288)\n"
           "  --dry-run             do not write, just compute and print\n");
}

int cmd_sdboot(int argc, char **argv)
{
    struct rk_sd_params p = { .mode = RK_SD_MODE_SDBOOT, .userdisk_label = "RK_SDBOOT" };
    const char *device = NULL, *image_out = NULL;
    uint64_t target_bytes = 0;
    int xz_preset = 6;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--image") && i+1 < argc)       p.update_img_path = argv[++i];
        else if (!strcmp(argv[i], "--sdboot") && i+1 < argc) p.sdboot_bin_path = argv[++i];
        else if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--image-out") && i+1 < argc) image_out = argv[++i];
        else if (!strcmp(argv[i], "--size-gb") && i+1 < argc)
            target_bytes = strtoull(argv[++i], NULL, 10) * 1024ULL*1024*1024;
        else if (!strcmp(argv[i], "--xz") && i+1 < argc) xz_preset = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--use-fw-loader")) p.use_fw_loader = 1;
        else if (!strcmp(argv[i], "--dry-run")) p.no_write = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (!p.update_img_path || (!device && !image_out) ||
        (!p.sdboot_bin_path && !p.use_fw_loader)) { usage(); return 1; }

    p.target_size_bytes = target_bytes;

    uint64_t required = 0;
    if (rk_sd_compute_size(&p, &required) != 0) {
        rk_err("failed to compute required size\n");
        return 1;
    }

    uint64_t sink_bytes = 0;
    struct rk_sink *s = NULL;

    if (image_out) {
        if (!target_bytes) {
            rk_err("sd-boot image-out needs --size-gb (default SD size)\n");
            return 1;
        }
        sink_bytes = target_bytes;
        if (sink_bytes < required) {
            rk_err("--size-gb too small: need at least %llu bytes\n",
                   (unsigned long long)required);
            return 1;
        }
        if (rk_path_has_suffix_xz(image_out)) {
            s = rk_sink_open_xz(image_out, xz_preset);
            if (s) rk_sink_set_size(s, sink_bytes);
        } else {
            s = rk_sink_open_image(image_out, sink_bytes);
        }
        if (!s) { rk_err("cannot open output %s\n", image_out); return 1; }
    } else {
        struct rk_disk *d = rk_disk_open(device);
        if (!d) return 1;
        uint64_t disk_bytes = rk_disk_size(d);
        sink_bytes = target_bytes ? target_bytes : disk_bytes;
        if (sink_bytes > disk_bytes) sink_bytes = disk_bytes;
        if (sink_bytes < required) {
            rk_err("target %s is too small: need %llu bytes, have %llu\n",
                   device, (unsigned long long)required,
                   (unsigned long long)sink_bytes);
            rk_disk_close(d);
            return 1;
        }
        if (!p.no_write && rk_disk_lock_volumes(d) != 0) {
            rk_err("failed to lock/dismount volumes on %s\n", device);
            rk_disk_close(d);
            return 1;
        }
        s = rk_sink_open_disk(d);
        if (!s) { rk_disk_close(d); return 1; }
        rk_sink_set_size(s, sink_bytes);
    }

    int rc = p.no_write ? 0 : rk_sd_create(s, &p);
    rk_sink_close(s);
    return rc;
}
