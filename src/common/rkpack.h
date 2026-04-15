#ifndef RK_PACK_H
#define RK_PACK_H

#include "rk_types.h"
#include "rkimg.h"

struct rk_pack_params {
    const char *src_dir;           /* unpacked tree: parameter, package-file, Image dir */
    const char *update_img_path;   /* RKAF / RKFW input (alternative to src_dir) */
    const char *loader_path;       /* optional; used when wrapping RKFW around RKAF */
    const char *out_path;          /* .img or .img.xz */
    int        write_rkfw;         /* if non-zero, wrap in RKFW (requires loader) */
    int        xz_preset;          /* 0-9 */
    uint32_t   chip;               /* rk chip id field for rkfw header */
    uint32_t   rom_version;
};

/*
 * Pack a directory (afptool-style "-pack") into an RKAF update.img. If
 * write_rkfw is set, additionally wrap the result in the RKFW rom.img format.
 * Output is streamed directly into .xz when out_path ends with ".xz".
 */
int rk_pack_from_dir(const struct rk_pack_params *p);

/*
 * Re-pack an existing update.img (RKAF or RKFW) into a destination, optionally
 * compressed. Used for "pack --image in.img -o out.img.xz".
 */
int rk_pack_from_image(const struct rk_pack_params *p);

#endif
