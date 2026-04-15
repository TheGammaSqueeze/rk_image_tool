#ifndef RK_SDCARD_H
#define RK_SDCARD_H

#include "rk_types.h"

struct rk_disk;

enum rk_sd_mode {
    RK_SD_MODE_SDBOOT    = 1,      /* plain SD-boot (dev debugging) */
    RK_SD_MODE_UPGRADE   = 2,      /* SD upgrade disk (auto-flash device) */
};

struct rk_sd_params {
    enum rk_sd_mode mode;

    const char *update_img_path;   /* required. The RKFW .img file. */
    const char *sdboot_bin_path;   /* required unless use_fw_loader is set. */
    const char *demo_path;         /* optional. Copied to user disk. */
    const char *sd_boot_config;    /* optional. sd_boot_config.config template. */

    int use_fw_loader;             /* 1 = write the MiniLoader from update.img
                                        instead of SDBoot.bin (RK3288 only). */
    int skip_userdisk_format;
    int no_write;                  /* dry run */

    const char *userdisk_label;
};

int rk_sd_create(struct rk_disk *d, const struct rk_sd_params *p);
int rk_sd_restore(struct rk_disk *d);

#endif
