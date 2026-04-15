#include "cli.h"
#include "../common/disk.h"
#include "../common/sdcard.h"
#include "../common/sink.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void read_line(char *buf, size_t bufsz)
{
    if (!fgets(buf, (int)bufsz, stdin)) { buf[0] = 0; return; }
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
}

static int prompt_path(const char *label, char *out, size_t outsz)
{
    printf("%s: ", label);
    fflush(stdout);
    read_line(out, outsz);
    return out[0] ? 0 : -1;
}

static void list_disks_simple(void)
{
    struct rk_disk_info info[32];
    size_t n = 0;
    if (rk_disk_list(info, 32, &n) != 0 || n == 0) {
        printf("  (no removable disks found)\n");
        return;
    }
    int printed = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!info[i].removable) continue;
        double gb = info[i].size_bytes / (1024.0 * 1024.0 * 1024.0);
        printf("  %zu) %-32s %.2f GiB  %s\n",
               i + 1, info[i].path, gb, info[i].model);
        ++printed;
    }
    if (!printed) printf("  (no removable disks found)\n");
}

static int run_sdcard(enum rk_sd_mode mode)
{
    char img[512], loader[512], dev[256], label[64];
    if (prompt_path("update.img path", img, sizeof(img)) != 0) return 1;
    printf("use MiniLoader from update.img instead of a separate SDBoot.bin? [y/N] ");
    fflush(stdout);
    char yn[16]; read_line(yn, sizeof(yn));
    int use_fw_loader = (yn[0] == 'y' || yn[0] == 'Y');
    loader[0] = 0;
    if (!use_fw_loader) {
        if (prompt_path("SDBoot.bin path", loader, sizeof(loader)) != 0) return 1;
    }

    printf("\navailable removable disks:\n");
    list_disks_simple();
    if (prompt_path("target device/image path (empty to cancel)", dev, sizeof(dev)) != 0)
        return 1;

    snprintf(label, sizeof(label),
             mode == RK_SD_MODE_UPGRADE ? "RK_UPDATE" : "RK_SDBOOT");

    struct rk_sd_params p = {
        .mode = mode,
        .update_img_path = img,
        .sdboot_bin_path = use_fw_loader ? NULL : loader,
        .use_fw_loader = use_fw_loader,
        .userdisk_label = label,
    };

    uint64_t required = 0;
    if (rk_sd_compute_size(&p, &required) != 0) return 1;

    struct rk_disk *d = rk_disk_open(dev);
    if (!d) return 1;
    uint64_t dsz = rk_disk_size(d);
    if (dsz < required) {
        rk_err("target too small: need %llu bytes\n",
               (unsigned long long)required);
        rk_disk_close(d);
        return 1;
    }
    struct rk_sink *s = rk_sink_open_disk(d);
    if (!s) { rk_disk_close(d); return 1; }
    rk_sink_set_size(s, dsz);
    int rc = rk_sd_create(s, &p);
    rk_sink_close(s);
    return rc;
}

int cmd_tui(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (;;) {
        printf("\n=== rk_image_tool ===\n"
               "  1) Info about an update.img\n"
               "  2) Unpack an update.img\n"
               "  3) Create an SD-boot card\n"
               "  4) Create an SD upgrade/flash card\n"
               "  5) Restore an SD card (wipe RK metadata)\n"
               "  6) List removable disks\n"
               "  7) Verify a card/image against an update.img\n"
               "  q) Quit\n"
               "choice> ");
        fflush(stdout);
        char buf[16];
        read_line(buf, sizeof(buf));
        if (!buf[0] || buf[0] == 'q' || buf[0] == 'Q') return 0;
        switch (buf[0]) {
        case '1': {
            char img[512];
            if (prompt_path("update.img path", img, sizeof(img)) == 0) {
                char *a[] = { "info", img };
                (void)cmd_info(2, a);
            }
            break;
        }
        case '2': {
            char img[512], dir[512];
            if (prompt_path("update.img path", img, sizeof(img)) == 0 &&
                prompt_path("output directory", dir, sizeof(dir)) == 0) {
                char *a[] = { "unpack", img, dir };
                (void)cmd_unpack(3, a);
            }
            break;
        }
        case '3': (void)run_sdcard(RK_SD_MODE_SDBOOT);  break;
        case '4': (void)run_sdcard(RK_SD_MODE_UPGRADE); break;
        case '5': {
            char dev[256];
            if (prompt_path("device path", dev, sizeof(dev)) == 0) {
                struct rk_disk *d = rk_disk_open(dev);
                if (d) { (void)rk_sd_restore(d); rk_disk_close(d); }
            }
            break;
        }
        case '6':
            list_disks_simple();
            break;
        case '7': {
            char img[512], dev[256], yn[16];
            if (prompt_path("update.img path", img, sizeof(img)) != 0) break;
            if (prompt_path("device or image path to verify", dev, sizeof(dev)) != 0) break;
            printf("skip misc partition (recommended for upgrade cards)? [Y/n] ");
            fflush(stdout);
            read_line(yn, sizeof(yn));
            int skip_misc = (yn[0] != 'n' && yn[0] != 'N');
            char *a[8]; int ac = 0;
            a[ac++] = (char *)"verify";
            a[ac++] = (char *)"--image"; a[ac++] = img;
            a[ac++] = (char *)"--device"; a[ac++] = dev;
            if (skip_misc) { a[ac++] = (char *)"--skip"; a[ac++] = (char *)"misc"; }
            (void)cmd_verify(ac, a);
            break;
        }
        default:
            printf("unknown choice\n");
        }
    }
}
