#ifndef RK_IMG_H
#define RK_IMG_H

#include "rk_types.h"
#include <stdio.h>

#define RKFW_MAGIC "RKFW"
#define RKAF_MAGIC "RKAF"
#define RKBOOT_MAGIC "BOOT"
#define RKLDR_MAGIC  "LDR "

RK_PACKED_BEGIN

struct rkfw_header {
    char     head_code[4];
    uint16_t head_len;
    uint32_t version;
    uint32_t code;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint32_t chip;
    uint32_t loader_offset;
    uint32_t loader_length;
    uint32_t image_offset;
    uint32_t image_length;
    uint32_t unknown1;
    uint32_t unknown2;
    uint32_t system_fstype;
    uint32_t backup_endpos;
    uint8_t  reserved[0x2D];
} RK_PACKED;

struct rkaf_part {
    char     name[32];
    char     filename[60];
    uint32_t nand_size;
    uint32_t pos;
    uint32_t nand_addr;
    uint32_t padded_size;
    uint32_t size;
} RK_PACKED;

struct rkaf_header {
    char     magic[4];
    uint32_t length;
    char     model[0x22];
    char     id[0x1E];
    char     manufacturer[0x38];
    uint32_t unknown1;
    uint32_t version;
    uint32_t num_parts;
    struct rkaf_part parts[16];
    uint8_t  reserved[0x74];
} RK_PACKED;

RK_PACKED_END

struct rk_image {
    FILE *fp;
    uint64_t file_size;
    int has_rkfw;
    struct rkfw_header rkfw;
    uint64_t rkaf_offset;
    uint64_t rkaf_length;
    struct rkaf_header rkaf;
};

int  rk_image_open(struct rk_image *img, const char *path);
void rk_image_close(struct rk_image *img);
int  rk_image_verify_md5(struct rk_image *img);

int  rk_image_export_loader(struct rk_image *img, const char *out_path);
int  rk_image_export_rkaf(struct rk_image *img, const char *out_path);

int  rk_image_find_part(const struct rk_image *img, const char *name, uint32_t *index);
int  rk_image_export_part(struct rk_image *img, uint32_t index, const char *out_path);
int  rk_image_read_part(struct rk_image *img, uint32_t index,
                        uint8_t **buf, uint64_t *len);

#endif
