#include "sdcard.h"
#include "disk.h"
#include "sink.h"
#include "rkimg.h"
#include "idblock.h"
#include "mbr.h"
#include "gpt.h"
#include "misc_cmd.h"
#include "parameter.h"
#include "fat32.h"
#include "progress.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RK_UPGRADE_FAT_OVERHEAD  (uint64_t)(128ULL * 1024ULL * 1024ULL)

struct idb_sink_ctx {
    struct rk_sink *s;
    int             no_write;
};

static int idb_sink_cb(void *user, uint64_t off, const void *buf, size_t len)
{
    struct idb_sink_ctx *c = (struct idb_sink_ctx *)user;
    if (c->no_write) return 0;
    return rk_sink_write(c->s, off, buf, len);
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

static int read_params_from_image(const struct rk_sd_params *p,
                                  struct rk_parameter *out)
{
    memset(out, 0, sizeof(*out));
    struct rk_image img;
    if (rk_image_open(&img, p->update_img_path) != 0) return -1;
    uint32_t pi = 0;
    if (rk_image_find_part(&img, "parameter", &pi) != 0) {
        rk_err("parameter partition not found in update.img\n");
        rk_image_close(&img);
        return -1;
    }
    uint8_t *wrapped = NULL; uint64_t wrapped_len = 0;
    if (rk_image_read_part(&img, pi, &wrapped, &wrapped_len) != 0) {
        rk_image_close(&img);
        return -1;
    }
    uint8_t *raw = NULL; size_t raw_len = 0;
    if (rk_parameter_unwrap(wrapped, wrapped_len, &raw, &raw_len) != 0) {
        raw = (uint8_t *)malloc(wrapped_len);
        if (!raw) { free(wrapped); rk_image_close(&img); return -1; }
        memcpy(raw, wrapped, wrapped_len);
        raw_len = wrapped_len;
    }
    int rc = rk_parameter_parse(raw, raw_len, out);
    free(wrapped); free(raw);
    rk_image_close(&img);
    return rc;
}

int rk_sd_compute_size(const struct rk_sd_params *p, uint64_t *out_min_bytes)
{
    struct rk_parameter params;
    if (read_params_from_image(p, &params) != 0) return -1;

    uint64_t fw_end_lba = (uint64_t)RK_IDB_SECTOR + 0x400ULL;
    uint64_t userdata_start_lba = 0;
    uint64_t userdata_min_sectors = 0;
    int has_grow = 0;
    for (size_t i = 0; i < params.num_parts; ++i) {
        const struct rk_part_entry *e = &params.parts[i];
        if (strncmp(e->name, "userdata", 31) == 0) {
            userdata_start_lba = e->offset_lba;
            userdata_min_sectors = e->size_lba;
            has_grow = e->grow;
        }
        uint64_t end;
        if (e->grow) end = e->offset_lba + 2048;
        else         end = (uint64_t)e->offset_lba + e->size_lba;
        if (end > fw_end_lba) fw_end_lba = end;
    }

    uint64_t min_sectors = fw_end_lba;
    if (p->mode == RK_SD_MODE_UPGRADE && userdata_start_lba) {
        uint64_t need_extra = 0;
        if (p->update_img_path) {
            FILE *fp = fopen(p->update_img_path, "rb");
            if (fp) { need_extra += rk_file_size(fp); fclose(fp); }
        }
        if (p->sd_boot_config && rk_path_exists(p->sd_boot_config)) {
            FILE *fp = fopen(p->sd_boot_config, "rb");
            if (fp) { need_extra += rk_file_size(fp); fclose(fp); }
        }
        if (p->demo_path && rk_path_exists(p->demo_path)) {
            FILE *fp = fopen(p->demo_path, "rb");
            if (fp) { need_extra += rk_file_size(fp); fclose(fp); }
        }
        need_extra += p->extra_userdata_bytes;
        need_extra += RK_UPGRADE_FAT_OVERHEAD;
        uint64_t need_sectors = (need_extra + RK_SECTOR_SIZE - 1) / RK_SECTOR_SIZE;
        uint64_t userdata_sectors = has_grow ? need_sectors : userdata_min_sectors;
        if (userdata_sectors < need_sectors) userdata_sectors = need_sectors;
        if (userdata_sectors < 66560) userdata_sectors = 66560;

        uint64_t total = userdata_start_lba + userdata_sectors;
        if (total > min_sectors) min_sectors = total;
    }

    min_sectors += 34;

    rk_parameter_free(&params);
    *out_min_bytes = min_sectors * RK_SECTOR_SIZE;
    return 0;
}

int rk_sd_create(struct rk_sink *s, const struct rk_sd_params *p)
{
    uint64_t dsz = rk_sink_size(s);
    if (dsz < (64ULL + 0x400ULL) * RK_SECTOR_SIZE) {
        rk_err("target is too small\n");
        return -1;
    }
    uint64_t total_sectors = dsz / RK_SECTOR_SIZE;

    rk_log("target size: %llu bytes (%llu sectors)\n",
           (unsigned long long)dsz, (unsigned long long)total_sectors);

    rk_log("[1/9] open update.img\n");
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

    rk_log("[2/9] clear MBR/GPT area\n");
    if (rk_sink_zero(s, 0, 0x400ULL) != 0) goto fail;
    if (rk_sink_zero(s, 34ULL * RK_SECTOR_SIZE,
                     (RK_IDB_SECTOR - 34ULL) * RK_SECTOR_SIZE) != 0) goto fail;
    if (rk_sink_zero(s, (total_sectors - 33) * RK_SECTOR_SIZE,
                     33ULL * RK_SECTOR_SIZE) != 0) goto fail;

    rk_log("[3/9] write protective MBR\n");
    uint8_t mbr[512];
    rk_mbr_build_protective(mbr, total_sectors);
    if (rk_sink_write(s, 0, mbr, 512) != 0) goto fail;

    rk_log("[4/9] write GPT\n");
    uint8_t phdr[512], bhdr[512];
    uint8_t *pents = (uint8_t *)calloc(1, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE);
    uint8_t *bents = (uint8_t *)calloc(1, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE);
    if (!pents || !bents) { free(pents); free(bents); goto fail; }
    if (rk_gpt_build(&params, total_sectors, phdr, pents, bhdr, bents) != 0) {
        free(pents); free(bents); goto fail;
    }
    if (rk_sink_write(s, 1ULL * RK_SECTOR_SIZE, phdr, 512) != 0
     || rk_sink_write(s, 2ULL * RK_SECTOR_SIZE, pents,
                      RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE) != 0
     || rk_sink_write(s, (total_sectors - 33) * RK_SECTOR_SIZE,
                      bents, RK_GPT_ENTRIES * RK_GPT_ENTRY_SIZE) != 0
     || rk_sink_write(s, (total_sectors - 1) * RK_SECTOR_SIZE,
                      bhdr, 512) != 0) {
        free(pents); free(bents); goto fail;
    }
    free(pents); free(bents);

    rk_log("[5/9] write loader (IDBlock at sector 64)\n");
    uint8_t *loader = NULL; uint64_t loader_len = 0;
    if (load_loader_blob(p, &img, &loader, &loader_len) != 0) goto fail;
    struct idb_sink_ctx ictx = { .s = s, .no_write = 0 };
    if (rk_idb_write(loader, loader_len, idb_sink_cb, &ictx) != 0) {
        free(loader);
        goto fail;
    }
    free(loader);

    rk_log("[6/9] write firmware partitions from update.img\n");
    static const char * const fw_parts[] = {
        "parameter", "uboot", "misc", "dtbo", "vbmeta",
        "boot", "recovery", "backup",
    };
    uint64_t fw_total = 0;
    for (size_t k = 0; k < sizeof(fw_parts)/sizeof(fw_parts[0]); ++k) {
        uint32_t idx;
        if (rk_image_find_part(&img, fw_parts[k], &idx) != 0) continue;
        struct rk_part_entry pe;
        if (rk_parameter_find(&params, fw_parts[k], &pe) != 0) continue;
        uint64_t ext_off = 0, ext_len = 0;
        if (rk_image_part_extent(&img, idx, &ext_off, &ext_len) != 0) continue;
        fw_total += ext_len;
    }
    struct rk_progress *pg_fw = rk_progress_start("firmware", fw_total);
    for (size_t k = 0; k < sizeof(fw_parts)/sizeof(fw_parts[0]); ++k) {
        const char *nm = fw_parts[k];
        uint32_t idx;
        if (rk_image_find_part(&img, nm, &idx) != 0) continue;
        struct rk_part_entry pe;
        if (rk_parameter_find(&params, nm, &pe) != 0) {
            rk_log("  skip %s (not in parameter.txt)\n", nm);
            continue;
        }
        uint64_t ext_off = 0, ext_len = 0;
        if (rk_image_part_extent(&img, idx, &ext_off, &ext_len) != 0) continue;
        rk_log("  %-10s -> LBA 0x%08x (%llu bytes)\n",
               nm, pe.offset_lba, (unsigned long long)ext_len);
        if (rk_sink_write_file(s, (uint64_t)pe.offset_lba * RK_SECTOR_SIZE,
                               p->update_img_path, ext_off, ext_len) != 0) {
            rk_progress_finish(pg_fw);
            goto fail;
        }
        rk_progress_add(pg_fw, ext_len);
    }
    rk_progress_finish(pg_fw);

    if (p->mode == RK_SD_MODE_UPGRADE) {
        struct rk_part_entry misc;
        if (rk_parameter_find(&params, "misc", &misc) == 0) {
            rk_log("[7/9] write misc rk_fwupdate command\n");
            uint8_t mbuf[0x2000];
            if (rk_misc_build_fwupdate(mbuf, sizeof(mbuf)) != 0) goto fail;
            if (rk_sink_write(s, (uint64_t)misc.offset_lba * RK_SECTOR_SIZE,
                              mbuf, sizeof(mbuf)) != 0)
                goto fail;
        }
    }

    if (p->mode == RK_SD_MODE_UPGRADE && !p->skip_userdisk_format) {
        struct rk_part_entry ud;
        if (rk_parameter_find(&params, "userdata", &ud) == 0) {
            uint64_t ud_size = ud.grow ? (total_sectors - 34 - ud.offset_lba)
                                       : ud.size_lba;
            ud_size &= ~(uint64_t)63ull;
            rk_log("[8/9] format userdata FAT32 at LBA 0x%08x (%llu sectors)\n",
                   ud.offset_lba, (unsigned long long)ud_size);
            struct rk_fat32_state fs;
            if (rk_fat32_format(s, &fs, ud.offset_lba, ud_size,
                                p->userdisk_label ? p->userdisk_label : "RK_UPDATE") != 0)
                goto fail;
            {
                FILE *ufp = fopen(p->update_img_path, "rb");
                uint64_t ufsz = ufp ? rk_file_size(ufp) : 0;
                if (ufp) fclose(ufp);
                struct rk_progress *pg_ud = rk_progress_start("update.img", ufsz);
                int arc = rk_fat32_add_file_progress(s, &fs, p->update_img_path,
                                                     "update.img", pg_ud);
                rk_progress_finish(pg_ud);
                if (arc != 0) goto fail;
            }
            if (p->sd_boot_config && rk_path_exists(p->sd_boot_config)) {
                (void)rk_fat32_add_file(s, &fs, p->sd_boot_config,
                                        "sd_boot_config.config");
            }
            if (p->demo_path && rk_path_exists(p->demo_path)) {
                const char *slash = strrchr(p->demo_path, '/');
                (void)rk_fat32_add_file(s, &fs, p->demo_path,
                                        slash ? slash + 1 : p->demo_path);
            }
        }
    }

    rk_log("[9/9] sync\n");

    rk_parameter_free(&params);
    free(param_wrapped); free(param_raw);
    rk_image_close(&img);

    rk_sink_sync(s);
    rk_log("done.\n");
    return 0;

fail:
    rk_parameter_free(&params);
    free(param_wrapped); free(param_raw);
    rk_image_close(&img);
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
    struct rk_sink *s = rk_sink_open_disk(d);
    if (!s) return -1;
    struct rk_fat32_state fs;
    int rc = rk_fat32_format(s, &fs, 2048, total_sectors - 2048, "RK_RESTORE");
    rk_sink_close(s);
    if (rc != 0) return -1;

    rk_disk_sync(d);
    rk_disk_rescan(d);
    rk_disk_release_volumes(d);
    rk_log("done.\n");
    return 0;
}
