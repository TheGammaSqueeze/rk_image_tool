#include "sink.h"
#include "disk.h"
#include "xz_writer.h"
#include "progress.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define FSEEKO _fseeki64
#else
#define FSEEKO fseeko
#endif

enum sink_kind { SINK_DISK, SINK_IMAGE, SINK_XZ };

enum region_kind { REGION_MEM, REGION_FILE, REGION_ZERO };

struct sink_region {
    uint64_t offset;
    uint64_t length;
    enum region_kind kind;
    uint8_t *data;
    char *src_path;
    uint64_t src_off;
    FILE *src_fp;
};

struct rk_sink {
    enum sink_kind kind;

    struct rk_disk *disk;
    int owns_disk;

    FILE *img_fp;
    char *img_path;

    xz_writer_t *xz;
    FILE *xz_fp;
    char *xz_path;

    uint64_t size;
    uint64_t required;

    struct sink_region *regions;
    size_t n_regions;
    size_t cap_regions;
};

int rk_path_has_suffix_xz(const char *path)
{
    if (!path) return 0;
    size_t n = strlen(path);
    if (n < 3) return 0;
    return (path[n-3] == '.' && (path[n-2] == 'x' || path[n-2] == 'X')
                              && (path[n-1] == 'z' || path[n-1] == 'Z'));
}

static void bump_required(struct rk_sink *s, uint64_t end)
{
    if (end > s->required) s->required = end;
}

static int region_push(struct rk_sink *s, struct sink_region r)
{
    if (s->n_regions == s->cap_regions) {
        size_t nc = s->cap_regions ? s->cap_regions * 2 : 64;
        struct sink_region *nb = (struct sink_region *)realloc(s->regions,
                                    nc * sizeof(*nb));
        if (!nb) return -1;
        s->regions = nb;
        s->cap_regions = nc;
    }
    s->regions[s->n_regions++] = r;
    return 0;
}

struct rk_sink *rk_sink_open_disk(struct rk_disk *d)
{
    struct rk_sink *s = (struct rk_sink *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kind = SINK_DISK;
    s->disk = d;
    s->owns_disk = 0;
    s->size = rk_disk_size(d);
    return s;
}

struct rk_sink *rk_sink_open_image(const char *path, uint64_t create_size)
{
    struct rk_sink *s = (struct rk_sink *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kind = SINK_IMAGE;
    s->img_fp = fopen(path, "wb+");
    if (!s->img_fp) {
        rk_err("open image %s for writing failed\n", path);
        free(s);
        return NULL;
    }
    s->img_path = strdup(path ? path : "");
    s->size = create_size;
    if (create_size) {
#if defined(_WIN32)
        _fseeki64(s->img_fp, (long long)create_size - 1, SEEK_SET);
#else
        fseeko(s->img_fp, (off_t)create_size - 1, SEEK_SET);
#endif
        uint8_t z = 0;
        fwrite(&z, 1, 1, s->img_fp);
        fflush(s->img_fp);
    }
    return s;
}

struct rk_sink *rk_sink_open_xz(const char *path, int preset)
{
    struct rk_sink *s = (struct rk_sink *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kind = SINK_XZ;
    s->xz_fp = fopen(path, "wb");
    if (!s->xz_fp) {
        rk_err("open %s for xz writing failed\n", path);
        free(s);
        return NULL;
    }
    s->xz = xz_writer_open(s->xz_fp, preset);
    if (!s->xz) {
        fclose(s->xz_fp);
        free(s);
        return NULL;
    }
    s->xz_path = strdup(path ? path : "");
    s->size = 0;
    return s;
}

int rk_sink_supports_read(struct rk_sink *s)
{
    return s && s->kind != SINK_XZ;
}

int rk_sink_set_size(struct rk_sink *s, uint64_t size)
{
    if (!s) return -1;
    if (size < s->required) return -1;
    s->size = size;
    if (s->kind == SINK_IMAGE && size > 0) {
#if defined(_WIN32)
        _fseeki64(s->img_fp, (long long)size - 1, SEEK_SET);
#else
        fseeko(s->img_fp, (off_t)size - 1, SEEK_SET);
#endif
        uint8_t z = 0;
        fwrite(&z, 1, 1, s->img_fp);
        fflush(s->img_fp);
    }
    return 0;
}

uint64_t rk_sink_size(struct rk_sink *s) { return s ? s->size : 0; }
uint64_t rk_sink_required_size(struct rk_sink *s) { return s ? s->required : 0; }

int rk_sink_write(struct rk_sink *s, uint64_t offset,
                  const void *buf, size_t len)
{
    if (!s || !buf) return -1;
    bump_required(s, offset + len);
    if (s->kind == SINK_DISK) {
        if (offset + len > s->size) {
            rk_err("write past end of disk\n");
            return -1;
        }
        return rk_disk_write(s->disk, offset, buf, len);
    }
    if (s->kind == SINK_IMAGE) {
        if (offset + len > s->size) {
            if (rk_sink_set_size(s, offset + len) != 0) return -1;
        }
#if defined(_WIN32)
        if (_fseeki64(s->img_fp, (long long)offset, SEEK_SET) != 0) return -1;
#else
        if (fseeko(s->img_fp, (off_t)offset, SEEK_SET) != 0) return -1;
#endif
        if (fwrite(buf, 1, len, s->img_fp) != len) return -1;
        return 0;
    }
    struct sink_region r = { .offset = offset, .length = len, .kind = REGION_MEM };
    r.data = (uint8_t *)malloc(len);
    if (!r.data) return -1;
    memcpy(r.data, buf, len);
    return region_push(s, r);
}

int rk_sink_write_file_progress(struct rk_sink *s, uint64_t offset,
                                const char *src_path, uint64_t src_off,
                                uint64_t len, struct rk_progress *pg)
{
    if (!s || !src_path) return -1;
    bump_required(s, offset + len);

    if (s->kind == SINK_DISK || s->kind == SINK_IMAGE) {
        FILE *fp = fopen(src_path, "rb");
        if (!fp) { rk_err("open %s: failed\n", src_path); return -1; }
        FSEEKO(fp, (long long)src_off, SEEK_SET);
        uint8_t buf[64 * 1024];
        uint64_t rem = len;
        uint64_t cur = offset;
        while (rem > 0) {
            size_t want = rem < sizeof(buf) ? (size_t)rem : sizeof(buf);
            size_t got = fread(buf, 1, want, fp);
            if (got == 0) { fclose(fp); return -1; }
            if (rk_sink_write(s, cur, buf, got) != 0) { fclose(fp); return -1; }
            cur += got;
            rem -= got;
            if (pg) rk_progress_add(pg, got);
        }
        fclose(fp);
        return 0;
    }

    struct sink_region r = { .offset = offset, .length = len, .kind = REGION_FILE };
    r.src_path = strdup(src_path);
    r.src_off = src_off;
    if (!r.src_path) return -1;
    int rv = region_push(s, r);
    if (rv == 0 && pg) rk_progress_add(pg, len);
    return rv;
}

int rk_sink_write_file(struct rk_sink *s, uint64_t offset,
                       const char *src_path, uint64_t src_off, uint64_t len)
{
    return rk_sink_write_file_progress(s, offset, src_path, src_off, len, NULL);
}

int rk_sink_zero(struct rk_sink *s, uint64_t offset, uint64_t len)
{
    if (!s || len == 0) return 0;
    bump_required(s, offset + len);
    if (s->kind == SINK_DISK) {
        if (offset + len > s->size) {
            rk_err("zero past end of disk\n");
            return -1;
        }
        return rk_disk_zero(s->disk, offset, len);
    }
    if (s->kind == SINK_IMAGE) {
        if (offset + len > s->size) {
            if (rk_sink_set_size(s, offset + len) != 0) return -1;
        }
        return 0;
    }
    struct sink_region r = { .offset = offset, .length = len, .kind = REGION_ZERO };
    return region_push(s, r);
}

int rk_sink_read(struct rk_sink *s, uint64_t offset, void *buf, size_t len)
{
    if (!s) return -1;
    if (s->kind == SINK_DISK) return rk_disk_read(s->disk, offset, buf, len);
    if (s->kind == SINK_IMAGE) {
#if defined(_WIN32)
        if (_fseeki64(s->img_fp, (long long)offset, SEEK_SET) != 0) return -1;
#else
        if (fseeko(s->img_fp, (off_t)offset, SEEK_SET) != 0) return -1;
#endif
        if (fread(buf, 1, len, s->img_fp) != len) return -1;
        return 0;
    }
    rk_err("read is not supported on streaming xz sinks\n");
    return -1;
}

int rk_sink_sync(struct rk_sink *s)
{
    if (!s) return 0;
    if (s->kind == SINK_DISK) return rk_disk_sync(s->disk);
    if (s->kind == SINK_IMAGE) { fflush(s->img_fp); return 0; }
    return 0;
}

struct emit_piece {
    uint64_t offset;
    uint64_t length;
    size_t   region_idx;
    uint64_t region_skip;
};

static int cmp_emit_piece(const void *pa, const void *pb)
{
    const struct emit_piece *a = (const struct emit_piece *)pa;
    const struct emit_piece *b = (const struct emit_piece *)pb;
    if (a->offset < b->offset) return -1;
    if (a->offset > b->offset) return 1;
    return 0;
}

static int xz_emit_zeros(xz_writer_t *xz, uint64_t n)
{
    static uint8_t zero_buf[64 * 1024];
    while (n > 0) {
        size_t want = n < sizeof(zero_buf) ? (size_t)n : sizeof(zero_buf);
        if (xz_writer_write(xz, zero_buf, want) != 0) return -1;
        n -= want;
    }
    return 0;
}

static int xz_emit_file(xz_writer_t *xz, const char *path, uint64_t off, uint64_t len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { rk_err("open %s: failed\n", path); return -1; }
    FSEEKO(fp, (long long)off, SEEK_SET);
    uint8_t buf[64 * 1024];
    while (len > 0) {
        size_t want = len < sizeof(buf) ? (size_t)len : sizeof(buf);
        size_t got = fread(buf, 1, want, fp);
        if (got == 0) { fclose(fp); return -1; }
        if (xz_writer_write(xz, buf, got) != 0) { fclose(fp); return -1; }
        len -= got;
    }
    fclose(fp);
    return 0;
}

static int flush_xz(struct rk_sink *s)
{
    size_t n = s->n_regions;
    size_t cap = n > 0 ? n * 2 : 1;
    struct emit_piece *pieces =
        (struct emit_piece *)malloc(cap * sizeof(*pieces));
    if (!pieces && cap) return -1;
    size_t npc = 0;

    /* Resolve overlaps: a later write overrides the bytes it covers in any
     * earlier region. Walk i from first to last; for each i, compute the
     * sub-intervals of region[i] not covered by any region[j] with j > i. */
    struct { uint64_t s, e, skip; } *scratch = NULL;
    size_t scratch_cap = 0;

    for (size_t i = 0; i < n; ++i) {
        struct sink_region *r = &s->regions[i];
        if (r->length == 0) continue;

        if (scratch_cap < 1) {
            scratch_cap = 8;
            scratch = realloc(scratch, scratch_cap * sizeof(*scratch));
            if (!scratch) { free(pieces); return -1; }
        }
        size_t nsc = 1;
        scratch[0].s = r->offset;
        scratch[0].e = r->offset + r->length;
        scratch[0].skip = 0;

        for (size_t j = i + 1; j < n && nsc > 0; ++j) {
            struct sink_region *o = &s->regions[j];
            if (o->length == 0) continue;
            uint64_t os = o->offset;
            uint64_t oe = o->offset + o->length;
            size_t k = 0;
            size_t added = 0;
            while (k < nsc) {
                uint64_t ss = scratch[k].s;
                uint64_t se = scratch[k].e;
                uint64_t sk = scratch[k].skip;
                if (oe <= ss || os >= se) { ++k; continue; }

                /* overlap: remove scratch[k], add non-overlapping pieces */
                uint64_t left_s = ss, left_e = (os > ss ? os : ss);
                uint64_t right_s = (oe < se ? oe : se), right_e = se;
                int has_left = left_e > left_s;
                int has_right = right_e > right_s;

                /* shift tail down by 1 to remove scratch[k] */
                if (k + 1 < nsc)
                    memmove(&scratch[k], &scratch[k + 1],
                            (nsc - k - 1) * sizeof(*scratch));
                --nsc;

                /* ensure capacity for up to 2 new pieces */
                while (nsc + added + 2 > scratch_cap) {
                    scratch_cap *= 2;
                    scratch = realloc(scratch, scratch_cap * sizeof(*scratch));
                    if (!scratch) { free(pieces); return -1; }
                }

                if (has_right) {
                    scratch[nsc].s = right_s;
                    scratch[nsc].e = right_e;
                    scratch[nsc].skip = sk + (right_s - ss);
                    ++nsc; ++added;
                }
                if (has_left) {
                    scratch[nsc].s = left_s;
                    scratch[nsc].e = left_e;
                    scratch[nsc].skip = sk;
                    ++nsc; ++added;
                }
            }
        }

        for (size_t k = 0; k < nsc; ++k) {
            if (npc == cap) {
                cap *= 2;
                pieces = realloc(pieces, cap * sizeof(*pieces));
                if (!pieces) { free(scratch); return -1; }
            }
            pieces[npc++] = (struct emit_piece){
                .offset = scratch[k].s,
                .length = scratch[k].e - scratch[k].s,
                .region_idx = i,
                .region_skip = scratch[k].skip
            };
        }
    }

    free(scratch);

    qsort(pieces, npc, sizeof(*pieces), cmp_emit_piece);

    uint64_t total = s->size ? s->size : s->required;
    uint64_t cursor = 0;
    for (size_t i = 0; i < npc; ++i) {
        struct emit_piece *p = &pieces[i];
        if (p->offset > cursor) {
            if (xz_emit_zeros(s->xz, p->offset - cursor) != 0) {
                free(pieces); return -1;
            }
            cursor = p->offset;
        }
        if (p->offset < cursor) {
            rk_err("internal error: overlap remained after resolve at %llu\n",
                   (unsigned long long)p->offset);
            free(pieces); return -1;
        }
        struct sink_region *r = &s->regions[p->region_idx];
        if (r->kind == REGION_MEM) {
            if (xz_writer_write(s->xz, r->data + p->region_skip, p->length) != 0) {
                free(pieces); return -1;
            }
        } else if (r->kind == REGION_FILE) {
            if (xz_emit_file(s->xz, r->src_path,
                             r->src_off + p->region_skip, p->length) != 0) {
                free(pieces); return -1;
            }
        } else {
            if (xz_emit_zeros(s->xz, p->length) != 0) {
                free(pieces); return -1;
            }
        }
        cursor += p->length;
    }

    free(pieces);

    if (total > cursor) {
        if (xz_emit_zeros(s->xz, total - cursor) != 0) return -1;
    }
    return 0;
}

int rk_sink_close(struct rk_sink *s)
{
    if (!s) return 0;
    int rv = 0;
    if (s->kind == SINK_XZ) {
        rv = flush_xz(s);
        if (xz_writer_close(s->xz) != 0) rv = -1;
        for (size_t i = 0; i < s->n_regions; ++i) {
            free(s->regions[i].data);
            free(s->regions[i].src_path);
        }
        free(s->regions);
        free(s->xz_path);
    } else if (s->kind == SINK_IMAGE) {
        fflush(s->img_fp);
        fclose(s->img_fp);
        free(s->img_path);
    } else if (s->kind == SINK_DISK) {
        if (s->owns_disk) rk_disk_close(s->disk);
    }
    free(s);
    return rv;
}
