#include "rkpack.h"
#include "crc.h"
#include "md5.h"
#include "rkimg.h"
#include "sink.h"
#include "util.h"
#include "xz_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define FSEEKO _fseeki64
#define FTELLO _ftelli64
#else
#define FSEEKO fseeko
#define FTELLO ftello
#endif

#define RK_PART_ALIGN  512u

struct pack_entry {
    char     name[32];
    char     filename[60];
    char     src_path[1024];
    uint32_t nand_addr;
    uint32_t nand_size;
    uint32_t pos;
    uint32_t size;
    uint32_t padded_size;
    int      is_reserved;
    int      is_parameter;
};

struct pack_plan {
    struct pack_entry entries[16];
    size_t n_entries;

    char model[0x22];
    char id[0x1E];
    char manufacturer[0x38];
    uint32_t version;

    uint64_t rkaf_body_size;    /* full RKAF body with CRC trailer */
    struct rkaf_header hdr;
};

/* pack_writer: abstracts RKAF output to either a regular FILE or an xz stream. */

struct pack_writer {
    FILE *plain;
    xz_writer_t *xz;
    struct md5_ctx md5;
    int hash;
    uint64_t total;
    uint32_t crc;       /* running RKCRC for RKAF trailer */
    int compute_crc;
};

static int pw_write_raw(struct pack_writer *w, const void *buf, size_t len)
{
    if (len == 0) return 0;
    if (w->hash) md5_update(&w->md5, (const uint8_t *)buf, len);
    if (w->xz) {
        if (xz_writer_write(w->xz, buf, len) != 0) return -1;
    } else {
        if (fwrite(buf, 1, len, w->plain) != len) return -1;
    }
    w->total += (uint64_t)len;
    return 0;
}

static int pw_write(struct pack_writer *w, const void *buf, size_t len)
{
    if (w->compute_crc && len > 0)
        w->crc = rk_crc32(w->crc, (const uint8_t *)buf, len);
    return pw_write_raw(w, buf, len);
}

static int pw_zero(struct pack_writer *w, uint64_t n)
{
    static uint8_t z[4096];
    while (n > 0) {
        size_t want = n < sizeof(z) ? (size_t)n : sizeof(z);
        if (pw_write(w, z, want) != 0) return -1;
        n -= want;
    }
    return 0;
}

static int pw_write_file(struct pack_writer *w, const char *path, uint64_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { rk_err("open %s: failed\n", path); return -1; }
    uint64_t n = 0;
    uint8_t buf[64 * 1024];
    for (;;) {
        size_t got = fread(buf, 1, sizeof(buf), fp);
        if (got == 0) break;
        if (pw_write(w, buf, got) != 0) { fclose(fp); return -1; }
        n += got;
    }
    fclose(fp);
    if (out_len) *out_len = n;
    return 0;
}

static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static uint64_t file_size_path(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    uint64_t sz = rk_file_size(fp);
    fclose(fp);
    return sz;
}

static int load_package_file(const char *srcdir, struct pack_plan *pl)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/package-file", srcdir);
    FILE *fp = fopen(path, "r");
    if (!fp) { rk_err("missing package-file at %s\n", path); return -1; }

    char line[512];
    size_t n = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == 0 || *p == '\r') continue;

        char name[31], file[59];
        if (sscanf(p, "%30s %58s", name, file) != 2) continue;

        if (n >= 16) { rk_err("too many partitions in package-file (max 16)\n"); fclose(fp); return -1; }
        struct pack_entry *e = &pl->entries[n++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", name);
        snprintf(e->filename, sizeof(e->filename), "%s", file);
        e->is_reserved = (strcmp(file, "RESERVED") == 0 ||
                          strcmp(file, "SELF")     == 0);
        e->is_parameter = (strcmp(name, "parameter") == 0);

        if (!e->is_reserved) {
            snprintf(e->src_path, sizeof(e->src_path), "%s/%s", srcdir, file);
            if (!rk_path_exists(e->src_path)) {
                rk_err("package entry references missing file: %s\n", e->src_path);
                fclose(fp);
                return -1;
            }
        }
    }
    fclose(fp);
    pl->n_entries = n;
    return 0;
}

static int find_parameter_path(const char *srcdir, char *out, size_t out_sz)
{
    static const char *cands[] = {
        "parameter", "parameter.txt",
        "Image/parameter", "Image/parameter.txt"
    };
    for (size_t i = 0; i < sizeof(cands)/sizeof(cands[0]); ++i) {
        snprintf(out, out_sz, "%s/%s", srcdir, cands[i]);
        if (rk_path_exists(out)) return 0;
    }
    return -1;
}

static int parse_mtdparts_into_plan(const char *param_txt, struct pack_plan *pl)
{
    /*
     * We look for the CMDLINE's "mtdparts=...:" payload and parse entries of
     * the form "size@addr(name)". Size and addr are in sectors (of 512 B),
     * the leading "-" size means "fill to end".
     */
    const char *cmd = strstr(param_txt, "CMDLINE:");
    if (!cmd) return -1;
    const char *p = strstr(cmd, "mtdparts=");
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    ++p;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        if (*p == 0 || *p == '\n' || *p == '\r') break;
        uint64_t size_sectors = 0;
        int fill = 0;
        if (*p == '-') { fill = 1; ++p; }
        else {
            while (*p && *p != '@' && *p != '(') {
                if (*p >= '0' && *p <= '9') {
                    size_sectors = size_sectors * 16 + (*p - '0');
                } else if (*p == 'x' || *p == 'X') {
                    /* skip */
                } else if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                    int v = (*p >= 'a' ? *p - 'a' + 10 : *p - 'A' + 10);
                    size_sectors = size_sectors * 16 + v;
                }
                ++p;
            }
        }
        if (*p != '@') break;
        ++p;
        uint64_t addr_sectors = 0;
        while (*p && *p != '(') {
            if (*p >= '0' && *p <= '9') addr_sectors = addr_sectors * 16 + (*p - '0');
            else if ((*p >= 'a' && *p <= 'f')) addr_sectors = addr_sectors * 16 + (*p - 'a' + 10);
            else if ((*p >= 'A' && *p <= 'F')) addr_sectors = addr_sectors * 16 + (*p - 'A' + 10);
            ++p;
        }
        if (*p != '(') break;
        ++p;
        const char *nm_start = p;
        while (*p && *p != ')') ++p;
        if (*p != ')') break;
        size_t nm_len = (size_t)(p - nm_start);
        ++p;

        char name[64];
        if (nm_len >= sizeof(name)) nm_len = sizeof(name) - 1;
        memcpy(name, nm_start, nm_len);
        name[nm_len] = 0;

        for (size_t k = 0; k < pl->n_entries; ++k) {
            if (strcmp(pl->entries[k].name, name) == 0) {
                pl->entries[k].nand_addr = (uint32_t)addr_sectors;
                pl->entries[k].nand_size = fill ? 0xFFFFFFFFu : (uint32_t)size_sectors;
                break;
            }
        }
    }
    return 0;
}

static int stage_parameter(char *dst, size_t dst_sz,
                           const char *src_path)
{
    (void)dst_sz;
    FILE *fp = fopen(src_path, "rb");
    if (!fp) { rk_err("open %s\n", src_path); return -1; }
    fclose(fp);
    snprintf(dst, dst_sz, "%s", src_path);
    return 0;
}

/*
 * If the buffer already starts with the PARM header, strip it so we can wrap
 * freshly. rk_image_tool unpack emits the wrapped form today.
 */
static void strip_parm_if_present(uint8_t **buf, uint64_t *len)
{
    if (*len < 12) return;
    const uint8_t *b = *buf;
    if (b[0] != 'P' || b[1] != 'A' || b[2] != 'R' || b[3] != 'M') return;
    uint32_t inner = (uint32_t)b[4] | ((uint32_t)b[5] << 8) |
                     ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    if ((uint64_t)inner + 8 + 4 > *len) return;
    uint8_t *raw = (uint8_t *)malloc(inner + 1);
    if (!raw) return;
    memcpy(raw, b + 8, inner);
    raw[inner] = 0;
    free(*buf);
    *buf = raw;
    *len = inner;
}

/*
 * Wrap the on-disk parameter.txt into the Rockchip "PARM" container:
 *   "PARM" (4) + u32 length + raw parameter bytes + u32 CRC + pad to 4 bytes.
 * Returns a malloced buffer plus its length. Free with free().
 */
static int build_parameter_wrapped(const char *src_path,
                                   uint8_t **out_buf, size_t *out_len)
{
    uint8_t *raw = NULL; uint64_t rawlen = 0;
    if (rk_read_all(src_path, &raw, &rawlen) != 0) return -1;
    strip_parm_if_present(&raw, &rawlen);

    size_t content = 8 + (size_t)rawlen + 4;
    size_t padded = (content + 3) & ~(size_t)3;
    uint8_t *buf = (uint8_t *)calloc(1, padded);
    if (!buf) { free(raw); return -1; }

    memcpy(buf + 0, "PARM", 4);
    uint32_t rl = (uint32_t)rawlen;
    buf[4] = (uint8_t)rl;
    buf[5] = (uint8_t)(rl >> 8);
    buf[6] = (uint8_t)(rl >> 16);
    buf[7] = (uint8_t)(rl >> 24);
    memcpy(buf + 8, raw, rawlen);
    uint32_t crc = 0;
    crc = rk_crc32(crc, raw, (size_t)rawlen);
    uint32_t tail_off = 8 + (uint32_t)rawlen;
    buf[tail_off + 0] = (uint8_t)crc;
    buf[tail_off + 1] = (uint8_t)(crc >> 8);
    buf[tail_off + 2] = (uint8_t)(crc >> 16);
    buf[tail_off + 3] = (uint8_t)(crc >> 24);

    free(raw);
    *out_buf = buf;
    *out_len = padded;
    return 0;
}

/*
 * Walk the plan and fill in pos/size/padded_size for every entry (except
 * reserved). rkaf_body_size is set to total bytes through the CRC trailer.
 */
static int compute_layout(struct pack_plan *pl, const char *srcdir,
                          uint8_t **param_wrapped_out, size_t *param_wrapped_len_out)
{
    uint64_t pos = sizeof(struct rkaf_header);

    for (size_t i = 0; i < pl->n_entries; ++i) {
        struct pack_entry *e = &pl->entries[i];
        if (e->is_reserved) {
            e->pos = 0xFFFFFFFFu;
            e->size = 0xFFFFFFFFu;
            e->padded_size = 0;
            continue;
        }
        uint64_t fsz = 0;
        if (e->is_parameter) {
            uint8_t *wrapped = NULL; size_t wlen = 0;
            char ppath[1000];
            if (find_parameter_path(srcdir, ppath, sizeof(ppath)) != 0) {
                rk_err("parameter file not found under %s\n", srcdir);
                return -1;
            }
            if (build_parameter_wrapped(ppath, &wrapped, &wlen) != 0) return -1;
            snprintf(e->src_path, sizeof(e->src_path), "%s.wrapped", ppath);
            /* Stash in out-params so caller can emit later. */
            *param_wrapped_out = wrapped;
            *param_wrapped_len_out = wlen;
            fsz = wlen;
        } else {
            fsz = file_size_path(e->src_path);
            if (fsz == 0) { rk_err("empty source: %s\n", e->src_path); return -1; }
        }
        if (fsz > 0xFFFFFFFFu) {
            rk_err("partition too large (>4GiB) not yet supported in packer: %s\n", e->name);
            return -1;
        }
        e->size = (uint32_t)fsz;
        uint32_t padded = align_up(e->size, RK_PART_ALIGN);
        e->padded_size = padded;
        e->pos = (uint32_t)pos;
        pos += padded;
    }

    pos += 4; /* CRC32 trailer */
    pl->rkaf_body_size = pos;
    return 0;
}

static void fill_rkaf_header(struct pack_plan *pl)
{
    struct rkaf_header *h = &pl->hdr;
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, RKAF_MAGIC, 4);
    h->length = (uint32_t)(pl->rkaf_body_size - 4); /* afptool convention */
    snprintf(h->model, sizeof(h->model), "%s", pl->model[0] ? pl->model : "RK29Device");
    snprintf(h->id, sizeof(h->id), "%s", pl->id[0] ? pl->id : "rockchip");
    snprintf(h->manufacturer, sizeof(h->manufacturer), "%s",
             pl->manufacturer[0] ? pl->manufacturer : "RockChip");
    h->version = pl->version ? pl->version : 0x01000000;
    h->num_parts = (uint32_t)pl->n_entries;
    for (size_t i = 0; i < pl->n_entries; ++i) {
        struct pack_entry *e = &pl->entries[i];
        struct rkaf_part *p = &h->parts[i];
        snprintf(p->name, sizeof(p->name), "%s", e->name);
        snprintf(p->filename, sizeof(p->filename), "%s", e->filename);
        p->nand_size = e->nand_size;
        p->pos = e->pos;
        p->nand_addr = e->nand_addr ? e->nand_addr : 0xFFFFFFFFu;
        p->padded_size = e->padded_size;
        p->size = e->size;
    }
}

static int emit_rkaf(struct pack_plan *pl, const char *srcdir,
                     struct pack_writer *w,
                     const uint8_t *param_wrapped, size_t param_wrapped_len)
{
    uint8_t *hdr = (uint8_t *)&pl->hdr;
    size_t hdr_len = sizeof(pl->hdr);

    w->crc = 0;
    w->compute_crc = 1;

    if (pw_write(w, hdr, hdr_len) != 0) return -1;

    for (size_t i = 0; i < pl->n_entries; ++i) {
        struct pack_entry *e = &pl->entries[i];
        if (e->is_reserved) continue;

        uint64_t expect_pos = e->pos;
        if (w->total != expect_pos) {
            rk_err("pack layout drift: pos mismatch for %s\n", e->name);
            return -1;
        }

        if (e->is_parameter) {
            if (pw_write(w, param_wrapped, param_wrapped_len) != 0) return -1;
        } else {
            if (pw_write_file(w, e->src_path, NULL) != 0) return -1;
        }
        uint64_t pad = (uint64_t)e->padded_size - (uint64_t)e->size;
        if (pad) {
            if (pw_zero(w, pad) != 0) return -1;
        }
    }

    w->compute_crc = 0;
    uint32_t crc = w->crc;
    uint8_t tail[4] = {
        (uint8_t)crc,
        (uint8_t)(crc >> 8),
        (uint8_t)(crc >> 16),
        (uint8_t)(crc >> 24),
    };
    if (pw_write_raw(w, tail, 4) != 0) return -1;

    (void)srcdir;
    return 0;
}

static int emit_rkfw_header(struct pack_writer *w,
                            uint32_t chip, uint32_t rom_version,
                            uint64_t loader_len, uint64_t rkaf_len)
{
    uint8_t buf[0x66];
    memset(buf, 0, sizeof(buf));
    memcpy(buf + 0, "RKFW", 4);
    buf[4] = 0x66;            /* head_len low */
    buf[5] = 0x00;
    uint32_t version = rom_version ? rom_version : 0x03000201u;
    buf[6] = (uint8_t)(version);
    buf[7] = (uint8_t)(version >> 8);
    buf[8] = (uint8_t)(version >> 16);
    buf[9] = (uint8_t)(version >> 24);
    uint32_t code = 0x01030000;
    buf[10] = (uint8_t)(code);
    buf[11] = (uint8_t)(code >> 8);
    buf[12] = (uint8_t)(code >> 16);
    buf[13] = (uint8_t)(code >> 24);

    time_t now = time(NULL);
    struct tm tm;
    struct tm *lt = localtime(&now);
    if (!lt) return -1;
    tm = *lt;
    uint16_t year = (uint16_t)(tm.tm_year + 1900);
    buf[14] = (uint8_t)year;
    buf[15] = (uint8_t)(year >> 8);
    buf[16] = (uint8_t)(tm.tm_mon + 1);
    buf[17] = (uint8_t)tm.tm_mday;
    buf[18] = (uint8_t)tm.tm_hour;
    buf[19] = (uint8_t)tm.tm_min;
    buf[20] = (uint8_t)tm.tm_sec;

    uint32_t chipv = chip ? chip : 0x50000000u;
    buf[21] = (uint8_t)chipv;
    buf[22] = (uint8_t)(chipv >> 8);
    buf[23] = (uint8_t)(chipv >> 16);
    buf[24] = (uint8_t)(chipv >> 24);

    uint32_t loader_offset = 0x66;
    uint32_t loader_length = (uint32_t)loader_len;
    uint32_t image_offset = loader_offset + loader_length;
    uint32_t image_length = (uint32_t)rkaf_len;
    buf[25] = (uint8_t)loader_offset;
    buf[26] = (uint8_t)(loader_offset >> 8);
    buf[27] = (uint8_t)(loader_offset >> 16);
    buf[28] = (uint8_t)(loader_offset >> 24);
    buf[29] = (uint8_t)loader_length;
    buf[30] = (uint8_t)(loader_length >> 8);
    buf[31] = (uint8_t)(loader_length >> 16);
    buf[32] = (uint8_t)(loader_length >> 24);
    buf[33] = (uint8_t)image_offset;
    buf[34] = (uint8_t)(image_offset >> 8);
    buf[35] = (uint8_t)(image_offset >> 16);
    buf[36] = (uint8_t)(image_offset >> 24);
    buf[37] = (uint8_t)image_length;
    buf[38] = (uint8_t)(image_length >> 8);
    buf[39] = (uint8_t)(image_length >> 16);
    buf[40] = (uint8_t)(image_length >> 24);
    /* unknown1, unknown2, system_fstype, backup_endpos left as zero */
    return pw_write_raw(w, buf, sizeof(buf));
}

static int write_output(const struct rk_pack_params *p,
                        int (*emit)(struct pack_writer *, void *), void *user,
                        uint64_t *total_out)
{
    FILE *plain = NULL;
    xz_writer_t *xz = NULL;

    if (rk_path_has_suffix_xz(p->out_path)) {
        FILE *xzout = fopen(p->out_path, "wb");
        if (!xzout) { rk_err("open %s\n", p->out_path); return -1; }
        xz = xz_writer_open(xzout, p->xz_preset > 0 ? p->xz_preset : 6);
        if (!xz) { fclose(xzout); return -1; }
    } else {
        plain = fopen(p->out_path, "wb");
        if (!plain) { rk_err("open %s\n", p->out_path); return -1; }
    }

    struct pack_writer w;
    memset(&w, 0, sizeof(w));
    w.plain = plain;
    w.xz = xz;

    int rc = emit(&w, user);
    if (total_out) *total_out = w.total;
    if (xz) (void)xz_writer_close(xz);
    else fclose(plain);
    return rc;
}

static int rk_path_has_suffix_xz_local(const char *path);

struct emit_rkfw_ctx {
    const struct rk_pack_params *p;
    struct pack_plan *pl;
    const char *srcdir;
    const uint8_t *param_wrapped;
    size_t param_wrapped_len;
    uint64_t loader_len;
    uint64_t rkaf_len;
    const char *loader_path;
};

static int emit_cb_rkaf(struct pack_writer *w, void *user)
{
    struct emit_rkfw_ctx *c = (struct emit_rkfw_ctx *)user;
    return emit_rkaf(c->pl, c->srcdir, w, c->param_wrapped, c->param_wrapped_len);
}

static int emit_cb_rkfw(struct pack_writer *w, void *user)
{
    struct emit_rkfw_ctx *c = (struct emit_rkfw_ctx *)user;

    w->hash = 1;
    md5_init(&w->md5);

    if (emit_rkfw_header(w, c->p->chip, c->p->rom_version, c->loader_len, c->rkaf_len) != 0)
        return -1;
    uint64_t wrote = 0;
    if (pw_write_file(w, c->loader_path, &wrote) != 0) return -1;
    if (wrote != c->loader_len) { rk_err("loader length drifted\n"); return -1; }
    if (emit_rkaf(c->pl, c->srcdir, w, c->param_wrapped, c->param_wrapped_len) != 0)
        return -1;

    uint8_t digest[16];
    md5_final(&w->md5, digest);
    char hex[33];
    md5_hex(digest, hex);
    w->hash = 0;
    return pw_write_raw(w, hex, 32);
}

static int compute_rkaf_size_only(struct pack_plan *pl,
                                  const char *srcdir,
                                  const uint8_t *param_wrapped,
                                  size_t param_wrapped_len,
                                  uint64_t *out_size)
{
    (void)param_wrapped;
    (void)param_wrapped_len;
    (void)srcdir;
    *out_size = pl->rkaf_body_size;
    return 0;
}

int rk_pack_from_dir(const struct rk_pack_params *p)
{
    if (!p || !p->src_dir || !p->out_path) {
        rk_err("rk_pack_from_dir: missing arguments\n");
        return -1;
    }

    struct pack_plan pl;
    memset(&pl, 0, sizeof(pl));

    if (load_package_file(p->src_dir, &pl) != 0) return -1;

    /* Load parameter file contents to parse mtdparts. */
    char param_path[1024];
    if (find_parameter_path(p->src_dir, param_path, sizeof(param_path)) != 0) {
        rk_err("parameter file not found under %s\n", p->src_dir);
        return -1;
    }
    uint8_t *param_raw = NULL; uint64_t param_raw_len = 0;
    if (rk_read_all(param_path, &param_raw, &param_raw_len) != 0) return -1;
    strip_parm_if_present(&param_raw, &param_raw_len);
    char *param_cstr = (char *)malloc(param_raw_len + 1);
    if (!param_cstr) { free(param_raw); return -1; }
    memcpy(param_cstr, param_raw, param_raw_len);
    param_cstr[param_raw_len] = 0;
    free(param_raw);
    parse_mtdparts_into_plan(param_cstr, &pl);
    free(param_cstr);

    uint8_t *param_wrapped = NULL;
    size_t param_wrapped_len = 0;
    if (compute_layout(&pl, p->src_dir, &param_wrapped, &param_wrapped_len) != 0)
        return -1;

    fill_rkaf_header(&pl);

    struct emit_rkfw_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.p = p;
    ctx.pl = &pl;
    ctx.srcdir = p->src_dir;
    ctx.param_wrapped = param_wrapped;
    ctx.param_wrapped_len = param_wrapped_len;
    ctx.rkaf_len = pl.rkaf_body_size;
    ctx.loader_path = p->loader_path;
    ctx.loader_len = ctx.loader_path ? file_size_path(ctx.loader_path) : 0;

    uint64_t total = 0;
    int rc;
    if (p->write_rkfw) {
        if (!ctx.loader_path || ctx.loader_len == 0) {
            rk_err("rkfw packaging requires a loader file\n");
            free(param_wrapped);
            return -1;
        }
        rc = write_output(p, emit_cb_rkfw, &ctx, &total);
    } else {
        rc = write_output(p, emit_cb_rkaf, &ctx, &total);
    }

    free(param_wrapped);
    return rc;
}

int rk_pack_from_image(const struct rk_pack_params *p)
{
    if (!p || !p->update_img_path || !p->out_path) return -1;

    FILE *in = fopen(p->update_img_path, "rb");
    if (!in) { rk_err("open %s\n", p->update_img_path); return -1; }
    uint64_t src_size = rk_file_size(in);

    FILE *plain = NULL;
    xz_writer_t *xz = NULL;
    int rc = 0;

    if (rk_path_has_suffix_xz(p->out_path)) {
        FILE *out = fopen(p->out_path, "wb");
        if (!out) { fclose(in); return -1; }
        xz = xz_writer_open(out, p->xz_preset > 0 ? p->xz_preset : 6);
        if (!xz) { fclose(in); fclose(out); return -1; }
    } else {
        plain = fopen(p->out_path, "wb");
        if (!plain) { fclose(in); return -1; }
    }

    uint8_t buf[64 * 1024];
    uint64_t remain = src_size;
    while (remain > 0) {
        size_t want = remain < sizeof(buf) ? (size_t)remain : sizeof(buf);
        size_t got = fread(buf, 1, want, in);
        if (got == 0) { rc = -1; break; }
        if (xz) {
            if (xz_writer_write(xz, buf, got) != 0) { rc = -1; break; }
        } else {
            if (fwrite(buf, 1, got, plain) != got) { rc = -1; break; }
        }
        remain -= got;
    }

    fclose(in);
    if (xz) xz_writer_close(xz);
    else fclose(plain);
    return rc;
}
