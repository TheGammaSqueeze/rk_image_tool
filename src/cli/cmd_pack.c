#include "cli.h"
#include "../common/rkimg.h"
#include "../common/util.h"

#include <stdio.h>

int cmd_pack(int argc, char **argv)
{
    (void)argc; (void)argv;
    /*
     * The first version of rk_image_tool re-uses the existing `afptool` +
     * `img_maker` pair for packing.  A native C port of the packer is on the
     * roadmap; see docs/ROADMAP.md.
     */
    printf("rk_image_tool pack is not implemented yet.\n"
           "Use afptool + img_maker from the RK2918 toolchain, or\n"
           "vendor's packaging scripts, to create update.img.\n");
    return 2;
}
