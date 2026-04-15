#ifndef RK_PARAMETER_H
#define RK_PARAMETER_H

#include "rk_types.h"

/* One mtdparts entry parsed from parameter.txt CMDLINE. */
struct rk_part_entry {
    char     name[32];
    uint32_t offset_lba;
    uint32_t size_lba;
    int      grow;
};

struct rk_parameter {
    char *raw;                 /* NUL-terminated text of parameter.txt */
    struct rk_part_entry *parts;
    size_t num_parts;
};

/* Parse a parameter.txt blob (raw bytes, may or may not have RK CRC prefix). */
int  rk_parameter_parse(const void *data, size_t len, struct rk_parameter *out);
void rk_parameter_free(struct rk_parameter *p);
int  rk_parameter_find(const struct rk_parameter *p, const char *name,
                       struct rk_part_entry *out);

/*
 * Wrap a raw parameter.txt into the RK param partition format:
 *   "PARM" <le32 size> <parameter.txt bytes> <le32 RKCRC>
 *
 * out is malloc()-allocated and must be freed by caller.
 */
int rk_parameter_wrap(const void *data, size_t len,
                      uint8_t **out, size_t *out_len);

/* Opposite: strip a RK-wrapped parameter.img back to raw text. */
int rk_parameter_unwrap(const void *data, size_t len,
                        uint8_t **out, size_t *out_len);

#endif
