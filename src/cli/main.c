#include "cli.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    int (*fn)(int, char **);
    const char *help;
} COMMANDS[] = {
    { "info",       cmd_info,       "print header info from an update.img" },
    { "unpack",     cmd_unpack,     "extract partitions from an update.img" },
    { "pack",       cmd_pack,       "pack an Image/ directory back into update.img" },
    { "sd-boot",    cmd_sdboot,     "create a plain SD-boot card" },
    { "upgrade",    cmd_upgrade,    "create an SD upgrade/flash card" },
    { "restore",    cmd_restore,    "restore an SD card back to a plain FAT32 disk" },
    { "list-disks", cmd_listdisks,  "list removable disks" },
    { "verify",     cmd_verify,     "read back a disk/image and compare to update.img" },
#ifdef RK_HAVE_TUI
    { "tui",        cmd_tui,        "launch interactive curses front-end" },
#endif
    { NULL, NULL, NULL }
};

static void usage(void)
{
    printf("rk_image_tool - cross-platform Rockchip firmware / SD tool\n\n"
           "usage: rk_image_tool <command> [args]\n\n"
           "commands:\n");
    for (int i = 0; COMMANDS[i].name; ++i)
        printf("  %-12s %s\n", COMMANDS[i].name, COMMANDS[i].help);
    printf("\nRun 'rk_image_tool <command> --help' for command-specific help.\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 1; }
    const char *cmd = argv[1];
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 ||
        strcmp(cmd, "help") == 0) {
        usage(); return 0;
    }
    for (int i = 0; COMMANDS[i].name; ++i) {
        if (strcmp(cmd, COMMANDS[i].name) == 0)
            return COMMANDS[i].fn(argc - 1, argv + 1);
    }
    fprintf(stderr, "unknown command: %s\n\n", cmd);
    usage();
    return 1;
}
