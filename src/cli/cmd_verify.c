#include "cli.h"
#include "../common/disk.h"
#include "../common/rkimg.h"
#include "../common/parameter.h"
#include "../common/idblock.h"
#include "../common/progress.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("usage: rk_image_tool verify [options]\n"
           "  --image <file>        update.img to compare against (required)\n"
           "  --device <path>       disk or image file to read from (required)\n"
           "  --skip-idblock        do not check IDBlock / loader\n"
           "  --skip <name>         do not check partition <name> (can repeat).\n"
           "                        Use --skip misc when verifying an upgrade card,\n"
           "                        since upgrade mode rewrites misc with an\n"
           "                        rk_fwupdate command.\n");
}

static int read_disk_range(struct rk_disk *d, uint64_t off, uint64_t len,
                           uint8_t *buf)
{
    uint64_t remain = len;
    uint64_t cursor = 0;
    uint8_t chunk[64 * 1024];
    while (remain) {
        uint64_t step = remain < sizeof(chunk) ? remain : sizeof(chunk);
        if (rk_disk_read(d, off + cursor, chunk, (size_t)step) != 0) return -1;
        memcpy(buf + cursor, chunk, (size_t)step);
        cursor += step;
        remain -= step;
    }
    return 0;
}

static int compare_range(struct rk_disk *d, uint64_t off, const uint8_t *src,
                         uint64_t len, const char *tag)
{
    uint64_t remain = len;
    uint64_t cursor = 0;
    uint8_t chunk[64 * 1024];
    struct rk_progress *pb = rk_progress_start(tag, len);
    while (remain) {
        uint64_t step = remain < sizeof(chunk) ? remain : sizeof(chunk);
        if (rk_disk_read(d, off + cursor, chunk, (size_t)step) != 0) {
            rk_progress_finish(pb);
            rk_err("read failed at 0x%llx\n", (unsigned long long)(off + cursor));
            return -1;
        }
        if (memcmp(chunk, src + cursor, (size_t)step) != 0) {
            rk_progress_finish(pb);
            for (size_t i = 0; i < step; ++i) {
                if (chunk[i] != src[cursor + i]) {
                    rk_err("%s: mismatch at file offset %llu (disk 0x%llx)\n",
                           tag, (unsigned long long)(cursor + i),
                           (unsigned long long)(off + cursor + i));
                    break;
                }
            }
            return -1;
        }
        cursor += step;
        remain -= step;
        rk_progress_update(pb, cursor);
    }
    rk_progress_finish(pb);
    return 0;
}

int cmd_verify(int argc, char **argv)
{
    const char *image_path = NULL;
    const char *device = NULL;
    int skip_idb = 0;
    const char *skips[32];
    int n_skips = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--image") && i + 1 < argc) image_path = argv[++i];
        else if (!strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--skip-idblock")) skip_idb = 1;
        else if (!strcmp(argv[i], "--skip") && i + 1 < argc) {
            if (n_skips < 32) skips[n_skips++] = argv[++i];
            else ++i;
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (!image_path || !device) { usage(); return 1; }

    struct rk_image img;
    if (rk_image_open(&img, image_path) != 0) return 1;

    uint32_t pi = 0;
    if (rk_image_find_part(&img, "parameter", &pi) != 0) {
        rk_err("parameter partition not found in update.img\n");
        rk_image_close(&img);
        return 1;
    }
    uint8_t *param_wrapped = NULL; uint64_t param_wrapped_len = 0;
    if (rk_image_read_part(&img, pi, &param_wrapped, &param_wrapped_len) != 0) {
        rk_image_close(&img);
        return 1;
    }
    uint8_t *param_raw = NULL; size_t param_raw_len = 0;
    if (rk_parameter_unwrap(param_wrapped, param_wrapped_len,
                            &param_raw, &param_raw_len) != 0) {
        param_raw = (uint8_t *)malloc(param_wrapped_len);
        if (!param_raw) { free(param_wrapped); rk_image_close(&img); return 1; }
        memcpy(param_raw, param_wrapped, param_wrapped_len);
        param_raw_len = param_wrapped_len;
    }
    struct rk_parameter params;
    if (rk_parameter_parse(param_raw, param_raw_len, &params) != 0) {
        free(param_wrapped); free(param_raw); rk_image_close(&img);
        return 1;
    }

    struct rk_disk *d = rk_disk_open(device);
    if (!d) {
        rk_parameter_free(&params);
        free(param_wrapped); free(param_raw);
        rk_image_close(&img);
        return 1;
    }

    int mismatches = 0;

    if (!skip_idb && img.has_rkfw) {
        uint64_t loader_len = img.rkfw.loader_length;
        if (loader_len) {
            uint8_t *loader = (uint8_t *)malloc((size_t)loader_len);
            if (loader) {
#if defined(_WIN32)
                _fseeki64(img.fp, img.rkfw.loader_offset, SEEK_SET);
#else
                fseeko(img.fp, img.rkfw.loader_offset, SEEK_SET);
#endif
                if (fread(loader, 1, (size_t)loader_len, img.fp) == loader_len) {
                    uint8_t *idb_img = NULL;
                    uint64_t idb_img_len = 0;
                    if (rk_idb_build(loader, loader_len, &idb_img, &idb_img_len) == 0) {
                        if (compare_range(d,
                                          (uint64_t)RK_IDB_SECTOR * RK_SECTOR_SIZE,
                                          idb_img, idb_img_len, "idblock") != 0) {
                            ++mismatches;
                        }
                        free(idb_img);
                    }
                }
                free(loader);
            }
        }
    }

    static const char * const fw_parts[] = {
        "parameter", "uboot", "misc", "dtbo", "vbmeta",
        "boot", "recovery", "backup",
    };
    for (size_t k = 0; k < sizeof(fw_parts)/sizeof(fw_parts[0]); ++k) {
        const char *nm = fw_parts[k];
        int skip = 0;
        for (int si = 0; si < n_skips; ++si)
            if (!strcmp(skips[si], nm)) { skip = 1; break; }
        if (skip) { rk_log("skipped %s\n", nm); continue; }
        uint32_t idx;
        if (rk_image_find_part(&img, nm, &idx) != 0) continue;
        struct rk_part_entry pe;
        if (rk_parameter_find(&params, nm, &pe) != 0) continue;
        uint8_t *buf = NULL; uint64_t buflen = 0;
        if (rk_image_read_part(&img, idx, &buf, &buflen) != 0) continue;
        if (compare_range(d, (uint64_t)pe.offset_lba * RK_SECTOR_SIZE,
                          buf, buflen, nm) != 0)
            ++mismatches;
        free(buf);
    }

    rk_disk_close(d);
    rk_parameter_free(&params);
    free(param_wrapped); free(param_raw);
    rk_image_close(&img);

    if (mismatches) {
        rk_err("verify FAILED (%d partition%s differ)\n",
               mismatches, mismatches == 1 ? "" : "s");
        return 1;
    }
    rk_log("verify OK\n");
    return 0;
}
