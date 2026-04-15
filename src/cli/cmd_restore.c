#include "cli.h"
#include "../common/sdcard.h"
#include "../common/disk.h"
#include "../common/util.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("usage: rk_image_tool restore --device <path>\n"
           "   or: rk_image_tool restore --image-out <file>\n");
}

int cmd_restore(int argc, char **argv)
{
    const char *device = NULL, *image_out = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
        else if (!strcmp(argv[i], "--image-out") && i+1 < argc) image_out = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }
    if (!device && !image_out) { usage(); return 1; }
    struct rk_disk *d = image_out
        ? rk_disk_open_image(image_out, 0)
        : rk_disk_open(device);
    if (!d) return 1;
    int rc = rk_sd_restore(d);
    rk_disk_close(d);
    return rc;
}
