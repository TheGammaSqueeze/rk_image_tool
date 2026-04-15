#include "rkimg.h"
#include "md5.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define FSEEKO _fseeki64
#define FTELLO _ftelli64
#else
#define FSEEKO fseeko
#define FTELLO ftello
#endif

int rk_image_open(struct rk_image *img, const char *path)
{
    memset(img, 0, sizeof(*img));
    img->fp = fopen(path, "rb");
    if (!img->fp) return -1;
    img->file_size = rk_file_size(img->fp);

    uint8_t magic[4];
    if (fread(magic, 1, 4, img->fp) != 4) goto fail;
    FSEEKO(img->fp, 0, SEEK_SET);

    if (memcmp(magic, RKFW_MAGIC, 4) == 0) {
        if (fread(&img->rkfw, 1, sizeof(img->rkfw), img->fp) != sizeof(img->rkfw))
            goto fail;
        img->has_rkfw = 1;
        img->rkaf_offset = img->rkfw.image_offset;
        img->rkaf_length = img->rkfw.image_length;
        FSEEKO(img->fp, (long long)img->rkaf_offset, SEEK_SET);
        if (fread(&img->rkaf, 1, sizeof(img->rkaf), img->fp) != sizeof(img->rkaf))
            goto fail;
    } else if (memcmp(magic, RKAF_MAGIC, 4) == 0) {
        img->has_rkfw = 0;
        img->rkaf_offset = 0;
        img->rkaf_length = img->file_size;
        if (fread(&img->rkaf, 1, sizeof(img->rkaf), img->fp) != sizeof(img->rkaf))
            goto fail;
    } else {
        rk_err("unknown image magic: %02x %02x %02x %02x\n",
               magic[0], magic[1], magic[2], magic[3]);
        goto fail;
    }

    if (memcmp(img->rkaf.magic, RKAF_MAGIC, 4) != 0) {
        rk_err("RKAF header not found inside update image\n");
        goto fail;
    }
    if (img->rkaf.num_parts > 16) {
        rk_err("rkaf header says num_parts=%u, max supported in struct is 16\n",
               img->rkaf.num_parts);
    }
    return 0;
fail:
    if (img->fp) fclose(img->fp);
    img->fp = NULL;
    return -1;
}

void rk_image_close(struct rk_image *img)
{
    if (img && img->fp) { fclose(img->fp); img->fp = NULL; }
}

static int verify_md5_at(struct rk_image *img, uint64_t limit)
{
    if (img->file_size < limit + 32) return -1;
    struct md5_ctx ctx;
    md5_init(&ctx);
    FSEEKO(img->fp, 0, SEEK_SET);
    uint8_t buf[8192];
    uint64_t remaining = limit;
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
        size_t got = fread(buf, 1, want, img->fp);
        if (got == 0) return -1;
        md5_update(&ctx, buf, got);
        remaining -= got;
    }
    uint8_t digest[16];
    md5_final(&ctx, digest);
    char hex_calc[33], hex_read[33];
    md5_hex(digest, hex_calc);
    if (fread(hex_read, 1, 32, img->fp) != 32) return -1;
    hex_read[32] = 0;
    for (int i = 0; i < 32; ++i) {
        char a = hex_calc[i];
        char b = hex_read[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return -1;
    }
    return 0;
}

int rk_image_verify_md5(struct rk_image *img)
{
    if (!img->has_rkfw) return 0;
    uint64_t limit = (uint64_t)img->rkfw.image_offset + (uint64_t)img->rkfw.image_length;
    if (verify_md5_at(img, limit) == 0) return 0;
    /* Fallback: some builds place MD5 at EOF-32 rather than image_end. */
    if (img->file_size > 32)
        return verify_md5_at(img, img->file_size - 32);
    return -1;
}

int rk_image_export_loader(struct rk_image *img, const char *out_path)
{
    if (!img->has_rkfw) return -1;
    FILE *out = fopen(out_path, "wb");
    if (!out) return -1;
    FSEEKO(img->fp, img->rkfw.loader_offset, SEEK_SET);
    uint64_t remaining = img->rkfw.loader_length;
    uint8_t buf[8192];
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
        size_t got = fread(buf, 1, want, img->fp);
        if (got == 0) { fclose(out); return -1; }
        if (fwrite(buf, 1, got, out) != got) { fclose(out); return -1; }
        remaining -= got;
    }
    fclose(out);
    return 0;
}

int rk_image_export_rkaf(struct rk_image *img, const char *out_path)
{
    FILE *out = fopen(out_path, "wb");
    if (!out) return -1;
    FSEEKO(img->fp, (long long)img->rkaf_offset, SEEK_SET);
    uint64_t remaining = img->rkaf_length;
    uint8_t buf[8192];
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
        size_t got = fread(buf, 1, want, img->fp);
        if (got == 0) { fclose(out); return -1; }
        if (fwrite(buf, 1, got, out) != got) { fclose(out); return -1; }
        remaining -= got;
    }
    fclose(out);
    return 0;
}

int rk_image_find_part(const struct rk_image *img, const char *name, uint32_t *index)
{
    uint32_t n = img->rkaf.num_parts > 16 ? 16 : img->rkaf.num_parts;
    for (uint32_t i = 0; i < n; ++i) {
        if (strncmp(img->rkaf.parts[i].name, name, sizeof(img->rkaf.parts[i].name)) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return -1;
}

int rk_image_read_part(struct rk_image *img, uint32_t index, uint8_t **buf, uint64_t *len)
{
    if (index >= img->rkaf.num_parts || index >= 16) return -1;
    const struct rkaf_part *p = &img->rkaf.parts[index];
    if (p->pos == 0xFFFFFFFFu || p->size == 0xFFFFFFFFu || p->size == 0) return -1;
    uint8_t *b = (uint8_t *)malloc(p->size);
    if (!b) return -1;
    FSEEKO(img->fp, (long long)(img->rkaf_offset + p->pos), SEEK_SET);
    if (fread(b, 1, p->size, img->fp) != p->size) { free(b); return -1; }
    *buf = b; *len = p->size;
    return 0;
}

int rk_image_part_extent(const struct rk_image *img, uint32_t index,
                         uint64_t *file_offset, uint64_t *length)
{
    if (index >= img->rkaf.num_parts || index >= 16) return -1;
    const struct rkaf_part *p = &img->rkaf.parts[index];
    if (p->pos == 0xFFFFFFFFu || p->size == 0xFFFFFFFFu || p->size == 0) return -1;
    if (file_offset) *file_offset = img->rkaf_offset + p->pos;
    if (length)      *length      = p->size;
    return 0;
}

int rk_image_export_part(struct rk_image *img, uint32_t index, const char *out_path)
{
    uint8_t *b = NULL; uint64_t n = 0;
    if (rk_image_read_part(img, index, &b, &n) != 0) return -1;
    int rc = rk_write_all(out_path, b, n);
    free(b);
    return rc;
}
