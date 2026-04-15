#include "sdcard.h"
#include "disk.h"
#include "rkimg.h"
#include "idblock.h"
#include "mbr.h"
#include "gpt.h"
#include "misc_cmd.h"
#include "parameter.h"
#include "fat32.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct write_ctx {
    struct rk_disk *d;
    int            no_write;
};

static int cb_write(void *user, uint64_t off, const void *buf, size_t len)
{
    struct write_ctx *c = (struct write_ctx *)user;
    if (c->no_write) return 0;
    return rk_disk_write(c->d, off, buf, len);
}

static int write_or_skip(struct rk_disk *d, int no_write, uint64_t off,
                         const void *buf, size_t len)
{
    if (no_write) return 0;
    return rk_disk_write(d, off, buf, len);
}

static int zero_or_skip(struct rk_disk *d, int no_write, uint64_t off, uint64_t len)
{
    if (no_write) return 0;
    return rk_disk_zero(d, off, len);
}

static int load_loader_blob(const struct rk_sd_params *p, struct rk_image *img,
                            uint8_t **out, uint64_t *out_len)
{
    if (p->use_fw_loader) {
        if (!img->has_rkfw) {
            rk_err("--use-fw-loader requires a full RKFW update.img\n");
            return -1;
        }
        uint8_t *b = (uint8_t *)malloc(img->rkfw.loader_length);
        if (!b) return -1;
#if defined(_WIN32)
        _fseeki64(img->fp, img->rkfw.loader_offset, SEEK_SET);
#else
        fseeko(img->fp, img->rkfw.loader_offset, SEEK_SET);
#endif
        if (fread(b, 1, img->rkfw.loader_length, img->fp) != img->rkfw.loader_length) {
            free(b);
            return -1;
        }
        *out = b;
        *out_len = img->rkfw.loader_length;
        return 0;
    }
    return rk_read_all(p->sdboot_bin_path, out, out_len);
}

int rk_sd_create(struct rk_disk *d, const struct rk_sd_params *p)
{
    uint64_t dsz = rk_disk_size(d);
    if (dsz < (64ULL + 0x400ULL) * RK_SECTOR_SIZE) {
        rk_err("disk is too small\n");
        return -1;
    }
    uint64_t total_sectors = dsz / RK_SECTOR_SIZE;

    rk_log("disk size : %llu bytes (%llu sectors)\n",
           (unsigned long long)dsz, (unsigned long long)total_sectors);

    rk_log("[1/9] eject / lock volumes\n");
    if (!p->no_write) {
        if (rk_disk_lock_volumes(d) != 0) {
            rk_err("failed to lock/dismount existing volumes\n");
            return -1;
        }
    }

    rk_log("[2/9] clear MBR/GPT area\n");
    if (zero_or_skip(d, p->no_write, 0, 0x400ULL) != 0) return -1;
    if (zero_or_skip(d, p->no_write, 34ULL * RK_SECTOR_SIZE,
                     (RK_IDB_SECTOR - 34ULL) * RK_SECTOR_SIZE) != 0) return -1;
    if (zero_or_skip(d, p->no_write, (total_sectors - 33) * RK_SECTOR_SIZE,
                     33ULL * RK_SECTOR_SIZE) != 0) return -1;

    rk_log("[3/9] open update.img\n");
    struct rk_image img;
    if (rk_image_open(&img, p->update_img_path) != 0) return -1;

    uint32_t pi = 0;
    if (rk_image_find_part(&img, "parameter", &pi) != 0) {
        rk_err("parameter partition not found in update.img\n");
        rk_image_close(&img);
        return -1;
    }
    uint8_t *param_wrapped = NULL; uint64_t param_wrapped_len = 0;
    if (rk_image_read_part(&img, pi, &param_wrapped, &param_wrapped_len) != 0) {
        rk_image_close(&img);
        return -1;
    }
    uint8_t *param_raw = NULL; size_t param_raw_len = 0;
    if (rk_parameter_unwrap(param_wrapped, param_wrapped_len,
                            &param_raw, &param_raw_len) != 0) {
        /* maybe it's raw already */
        param_raw = (uint8_t *)malloc(param_wrapped_len);
        if (!param_raw) { free(param_wrapped); rk_image_close(&img); return -1; }
        memcpy(param_raw, param_wrapped, param_wrapped_len);
        param_raw_len = param_wrapped_len;
    }
    struct rk_parameter params;
    if (rk_parameter_parse(param_raw, param_raw_len, &params) != 0) {
        rk_err("failed to parse parameter.txt\n");
        free(param_wrapped); free(param_raw);
        rk_image_close(&img);
        return -1;
    }
    rk_log("parsed %zu partitions from parameter.txt\n", params.num_parts);

    rk_log("[4/9] write protective MBR\n");
    uint8_t mbr[512];
    rk_mbr_build_protective(mbr, total_sectors);
    if (write_or_skip(d, p->no_write, 0, mbr, 512) != 0) goto fail;

    rk_log("[5/9] write GPT\n");
    uint8_t phdr[512], bhdr[512];
    uint8_t *pents = (uint8_t *)calloc(1, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE);
    uint8_t *bents = (uint8_t *)calloc(1, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE);
    if (!pents || !bents) { free(pents); free(bents); goto fail; }
    if (rk_gpt_build(&params, total_sectors, phdr, pents, bhdr, bents) != 0) {
        free(pents); free(bents); goto fail;
    }
    if (write_or_skip(d, p->no_write, 1ULL * RK_SECTOR_SIZE, phdr, 512) != 0
     || write_or_skip(d, p->no_write, 2ULL * RK_SECTOR_SIZE, pents,
                      RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE) != 0
     || write_or_skip(d, p->no_write, (total_sectors - 33) * RK_SECTOR_SIZE,
                      bents, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE) != 0
     || write_or_skip(d, p->no_write, (total_sectors - 1) * RK_SECTOR_SIZE,
                      bhdr, 512) != 0) {
        free(pents); free(bents); goto fail;
    }
    free(pents); free(bents);

    rk_log("[6/9] write loader (IDBlock at sector 64)\n");
    uint8_t *loader = NULL; uint64_t loader_len = 0;
    if (load_loader_blob(p, &img, &loader, &loader_len) != 0) goto fail;
    struct write_ctx ctx = { .d = d, .no_write = p->no_write };
    if (rk_idb_write(loader, loader_len, cb_write, &ctx) != 0) {
        free(loader);
        goto fail;
    }
    free(loader);

    rk_log("[7/9] write firmware partitions from update.img\n");
    static const char * const fw_parts[] = {
        "parameter", "uboot", "misc", "dtbo", "vbmeta",
        "boot", "recovery", "backup",
    };
    for (size_t k = 0; k < sizeof(fw_parts)/sizeof(fw_parts[0]); ++k) {
        const char *nm = fw_parts[k];
        uint32_t idx;
        if (rk_image_find_part(&img, nm, &idx) != 0) continue;
        struct rk_part_entry pe;
        if (rk_parameter_find(&params, nm, &pe) != 0) {
            rk_log("  skip %s (not in parameter.txt)\n", nm);
            continue;
        }
        uint8_t *buf = NULL; uint64_t buflen = 0;
        if (rk_image_read_part(&img, idx, &buf, &buflen) != 0) continue;
        rk_log("  %-10s -> LBA 0x%08x (%llu bytes)\n",
               nm, pe.offset_lba, (unsigned long long)buflen);
        if (write_or_skip(d, p->no_write,
                          (uint64_t)pe.offset_lba * RK_SECTOR_SIZE,
                          buf, (size_t)buflen) != 0) {
            free(buf);
            goto fail;
        }
        free(buf);
    }

    /* Override misc with rk_fwupdate command so the device auto-flashes */
    if (p->mode == RK_SD_MODE_UPGRADE) {
        struct rk_part_entry misc;
        if (rk_parameter_find(&params, "misc", &misc) == 0) {
            rk_log("[8/9] write misc rk_fwupdate command\n");
            uint8_t mbuf[0x2000];
            if (rk_misc_build_fwupdate(mbuf, sizeof(mbuf)) != 0) goto fail;
            if (write_or_skip(d, p->no_write,
                              (uint64_t)misc.offset_lba * RK_SECTOR_SIZE,
                              mbuf, sizeof(mbuf)) != 0)
                goto fail;
        }
    }

    /* Format userdata FAT32 and drop update.img + sd_boot_config in it */
    if (p->mode == RK_SD_MODE_UPGRADE && !p->skip_userdisk_format) {
        struct rk_part_entry ud;
        if (rk_parameter_find(&params, "userdata", &ud) == 0) {
            uint64_t ud_size = ud.grow ? (total_sectors - 34 - ud.offset_lba)
                                       : ud.size_lba;
            ud_size &= ~(uint64_t)63ull;
            rk_log("[9/9] format userdata FAT32 at LBA 0x%08x (%llu sectors)\n",
                   ud.offset_lba, (unsigned long long)ud_size);
            if (!p->no_write) {
                if (rk_fat32_format(d, ud.offset_lba, ud_size,
                                    p->userdisk_label ? p->userdisk_label : "RK_UPDATE") != 0)
                    goto fail;
                if (rk_fat32_add_file(d, ud.offset_lba,
                                      p->update_img_path,
                                      "SDUPDATEIMG") != 0)
                    goto fail;
                if (p->sd_boot_config && rk_path_exists(p->sd_boot_config)) {
                    (void)rk_fat32_add_file(d, ud.offset_lba, p->sd_boot_config,
                                            "SD_BOOT CFG");
                }
                if (p->demo_path && rk_path_exists(p->demo_path)) {
                    (void)rk_fat32_add_file(d, ud.offset_lba, p->demo_path,
                                            "DEMO    BIN");
                }
            }
        }
    }

    rk_parameter_free(&params);
    free(param_wrapped); free(param_raw);
    rk_image_close(&img);

    if (!p->no_write) {
        rk_disk_sync(d);
        rk_disk_rescan(d);
        rk_disk_release_volumes(d);
    }
    rk_log("done.\n");
    return 0;

fail:
    rk_parameter_free(&params);
    free(param_wrapped); free(param_raw);
    rk_image_close(&img);
    if (!p->no_write) rk_disk_release_volumes(d);
    return -1;
}

int rk_sd_restore(struct rk_disk *d)
{
    uint64_t dsz = rk_disk_size(d);
    uint64_t total_sectors = dsz / RK_SECTOR_SIZE;

    rk_log("restore: lock volumes\n");
    if (rk_disk_lock_volumes(d) != 0) return -1;

    rk_log("restore: zero loader region and metadata\n");
    if (rk_disk_zero(d, 0, 34ULL * RK_SECTOR_SIZE) != 0) return -1;
    if (rk_disk_zero(d, (uint64_t)RK_IDB_SECTOR * RK_SECTOR_SIZE,
                     0x400ULL * RK_SECTOR_SIZE) != 0) return -1;
    if (rk_disk_zero(d, (total_sectors - 33) * RK_SECTOR_SIZE,
                     33ULL * RK_SECTOR_SIZE) != 0) return -1;

    rk_log("restore: write fresh MBR with full-span FAT32\n");
    uint8_t mbr[512];
    rk_mbr_build_fat32(mbr, total_sectors, 2048);
    if (rk_disk_write(d, 0, mbr, 512) != 0) return -1;

    rk_log("restore: format FAT32 on partition\n");
    if (rk_fat32_format(d, 2048, total_sectors - 2048, "RK_RESTORE") != 0) return -1;

    rk_disk_sync(d);
    rk_disk_rescan(d);
    rk_disk_release_volumes(d);
    rk_log("done.\n");
    return 0;
}
