#ifndef RK_SDCARD_H
#define RK_SDCARD_H

#include "rk_types.h"

struct rk_disk;
struct rk_sink;

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

    uint64_t target_size_bytes;    /* 0 = auto (upgrade) / entire sink (sd-boot).
                                      Otherwise cap/expand userdata to hit this. */
    uint64_t extra_userdata_bytes; /* overhead for files we will copy into the
                                      userdata FAT32 on upgrade mode. */
};

/*
 * Compute the minimum number of bytes an image/disk must be to fit the SD
 * layout described by `p`. When target_size_bytes is 0 and mode is UPGRADE,
 * this is the value we use as the target. Populates *out_min_bytes. Returns 0
 * on success.
 */
int rk_sd_compute_size(const struct rk_sd_params *p, uint64_t *out_min_bytes);

/*
 * Write the full SD-boot / upgrade layout through `s`. The sink's current
 * size (rk_sink_size) is used for grow-to-end partition calculations; callers
 * must have sized it to at least rk_sd_compute_size().
 */
int rk_sd_create(struct rk_sink *s, const struct rk_sd_params *p);

/*
 * Wipe RK metadata from a disk and lay down a plain FAT32 volume.
 */
int rk_sd_restore(struct rk_disk *d);

#endif
