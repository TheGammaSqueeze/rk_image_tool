#include "cli.h"
#include "../common/disk.h"
#include "../common/util.h"

#include <stdio.h>
#include <string.h>

int cmd_listdisks(int argc, char **argv)
{
    int all = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--all") == 0) all = 1;
    }
    struct rk_disk_info info[64];
    size_t n = 0;
    if (rk_disk_list(info, 64, &n) != 0) {
        rk_err("failed to list disks\n");
        return 1;
    }
    printf("%-24s %-16s %-14s %-3s %s\n",
           "PATH", "NAME", "SIZE", "RMV", "MODEL");
    for (size_t i = 0; i < n; ++i) {
        if (!all && !info[i].removable) continue;
        double gb = (double)info[i].size_bytes / (1024.0 * 1024.0 * 1024.0);
        printf("%-24s %-16s %10.2f GiB %-3s %s\n",
               info[i].path, info[i].name, gb,
               info[i].removable ? "yes" : "no",
               info[i].model);
    }
    return 0;
}
