#include "cli.h"
#include "../common/parameter.h"
#include "../common/rkimg.h"
#include "../common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_unpack(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: rk_image_tool unpack <update.img> <out_dir>\n");
        return 1;
    }
    const char *in = argv[1];
    const char *dir = argv[2];
    rk_mkdir_p(dir);

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/Image", dir);
    rk_mkdir_p(sub);

    struct rk_image img;
    if (rk_image_open(&img, in) != 0) return 1;
    if (img.has_rkfw) {
        if (rk_image_verify_md5(&img) != 0)
            rk_err("warning: MD5 check failed\n");
    }

    uint32_t n = img.rkaf.num_parts > 16 ? 16 : img.rkaf.num_parts;

    char pf_path[600];
    snprintf(pf_path, sizeof(pf_path), "%s/package-file", dir);
    FILE *pf = fopen(pf_path, "w");
    if (pf) {
        fprintf(pf, "# NAME\tRelative path\n#\n");
        fprintf(pf, "package-file\tpackage-file\n");
    }

    for (uint32_t i = 0; i < n; ++i) {
        const struct rkaf_part *p = &img.rkaf.parts[i];
        if (strncmp(p->name, "package-file", 32) == 0) continue;
        if (strncmp(p->filename, "RESERVED", 8) == 0) {
            if (pf) fprintf(pf, "%.32s\tRESERVED\n", p->name);
            continue;
        }
        snprintf(sub, sizeof(sub), "%s/%.60s", dir, p->filename);
        char dparent[512];
        rk_dirname(dparent, sizeof(dparent), sub);
        rk_mkdir_p(dparent);

        printf("extracting %-20.32s -> %s\n", p->name, sub);
        if (rk_image_export_part(&img, i, sub) != 0) {
            rk_err("failed to export %s\n", p->name);
            rk_image_close(&img);
            if (pf) fclose(pf);
            return 1;
        }
        if (strncmp(p->name, "parameter", 32) == 0) {
            uint8_t *raw = NULL; uint64_t raw_len = 0;
            if (rk_read_all(sub, &raw, &raw_len) == 0) {
                uint8_t *unwrapped = NULL; size_t unwrapped_len = 0;
                if (rk_parameter_unwrap(raw, raw_len, &unwrapped, &unwrapped_len) == 0) {
                    rk_write_all(sub, unwrapped, unwrapped_len);
                    free(unwrapped);
                }
                free(raw);
            }
        }
        if (pf) fprintf(pf, "%.32s\t%.60s\n", p->name, p->filename);
    }
    if (pf) fclose(pf);
    rk_image_close(&img);
    return 0;
}
