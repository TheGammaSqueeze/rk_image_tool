#ifndef RK_CLI_H
#define RK_CLI_H

int cmd_info(int argc, char **argv);
int cmd_unpack(int argc, char **argv);
int cmd_pack(int argc, char **argv);
int cmd_sdboot(int argc, char **argv);
int cmd_upgrade(int argc, char **argv);
int cmd_restore(int argc, char **argv);
int cmd_listdisks(int argc, char **argv);
int cmd_verify(int argc, char **argv);
#ifdef RK_HAVE_TUI
int cmd_tui(int argc, char **argv);
#endif

#endif
