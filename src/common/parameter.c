#include "parameter.h"
#include "crc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t parse_hex(const char *s, const char **end)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t v = 0;
    while (1) {
        char c = *s;
        unsigned d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else break;
        v = v * 16 + d;
        s++;
    }
    if (end) *end = s;
    return v;
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int rk_parameter_parse(const void *data, size_t len, struct rk_parameter *out)
{
    memset(out, 0, sizeof(*out));

    const uint8_t *p = (const uint8_t *)data;
    size_t off = 0;
    size_t plen = len;

    if (len >= 8 && memcmp(p, "PARM", 4) == 0) {
        uint32_t sz = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                    | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        if (sz + 8 <= len) {
            off = 8;
            plen = sz;
        } else {
            return -1;
        }
    }

    out->raw = (char *)malloc(plen + 1);
    if (!out->raw) return -1;
    memcpy(out->raw, p + off, plen);
    out->raw[plen] = 0;

    const char *line = strstr(out->raw, "CMDLINE");
    if (!line) return 0;
    const char *mt = strstr(line, "mtdparts=");
    if (!mt) return 0;
    mt = strchr(mt, ':');
    if (!mt) return 0;
    mt++;

    size_t cap = 32;
    out->parts = (struct rk_part_entry *)calloc(cap, sizeof(*out->parts));
    if (!out->parts) return -1;

    while (*mt && *mt != '\n' && *mt != '\r') {
        mt = skip_ws(mt);
        const char *end_num = NULL;
        int grow = 0;
        uint32_t size_lba = 0;
        if (*mt == '-') { grow = 1; mt++; }
        else size_lba = parse_hex(mt, &end_num);
        if (end_num) mt = end_num;
        if (*mt != '@') break;
        mt++;
        uint32_t off_lba = parse_hex(mt, &end_num);
        if (end_num) mt = end_num;
        if (*mt != '(') break;
        mt++;
        const char *np = mt;
        while (*mt && *mt != ')' && *mt != ':') mt++;
        size_t nlen = (size_t)(mt - np);
        if (nlen >= sizeof(out->parts[0].name)) nlen = sizeof(out->parts[0].name) - 1;

        if (out->num_parts + 1 > cap) {
            cap *= 2;
            struct rk_part_entry *t = (struct rk_part_entry *)realloc(
                out->parts, cap * sizeof(*out->parts));
            if (!t) return -1;
            out->parts = t;
        }
        struct rk_part_entry *e = &out->parts[out->num_parts++];
        memset(e, 0, sizeof(*e));
        memcpy(e->name, np, nlen);
        e->offset_lba = off_lba;
        e->size_lba = size_lba;
        e->grow = grow;

        if (*mt == ':') {
            const char *tp = mt + 1;
            while (*tp && *tp != ')') tp++;
            size_t tlen = (size_t)(tp - (mt + 1));
            if (tlen == 4 && memcmp(mt + 1, "grow", 4) == 0) e->grow = 1;
            mt = tp;
        }
        if (*mt == ')') mt++;
        if (*mt == ',') mt++;
    }
    return 0;
}

void rk_parameter_free(struct rk_parameter *p)
{
    if (!p) return;
    free(p->raw); p->raw = NULL;
    free(p->parts); p->parts = NULL; p->num_parts = 0;
}

int rk_parameter_find(const struct rk_parameter *p, const char *name,
                      struct rk_part_entry *out)
{
    for (size_t i = 0; i < p->num_parts; ++i) {
        if (strcmp(p->parts[i].name, name) == 0) {
            *out = p->parts[i];
            return 0;
        }
    }
    return -1;
}

int rk_parameter_wrap(const void *data, size_t len,
                      uint8_t **out, size_t *out_len)
{
    size_t total = 8 + len + 4;
    uint8_t *b = (uint8_t *)malloc(total);
    if (!b) return -1;
    b[0]='P'; b[1]='A'; b[2]='R'; b[3]='M';
    b[4] = (uint8_t)(len);
    b[5] = (uint8_t)(len >> 8);
    b[6] = (uint8_t)(len >> 16);
    b[7] = (uint8_t)(len >> 24);
    memcpy(b + 8, data, len);
    uint32_t crc = 0;
    crc = rk_crc32(crc, data, len);
    b[8 + len + 0] = (uint8_t)(crc);
    b[8 + len + 1] = (uint8_t)(crc >> 8);
    b[8 + len + 2] = (uint8_t)(crc >> 16);
    b[8 + len + 3] = (uint8_t)(crc >> 24);
    *out = b;
    *out_len = total;
    return 0;
}

int rk_parameter_unwrap(const void *data, size_t len,
                        uint8_t **out, size_t *out_len)
{
    if (len < 12) return -1;
    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p, "PARM", 4) != 0) return -1;
    uint32_t sz = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    if (sz + 12 > len) return -1;
    uint8_t *b = (uint8_t *)malloc(sz);
    if (!b) return -1;
    memcpy(b, p + 8, sz);
    *out = b;
    *out_len = sz;
    return 0;
}
