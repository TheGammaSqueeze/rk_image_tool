#include "misc_cmd.h"
#include <string.h>

int rk_misc_build_fwupdate(uint8_t *buf, size_t buf_len)
{
    if (buf_len < 0x2000) return -1;
    memset(buf, 0, 0x2000);

    /* bootloader_message struct starts at +0x800 */
    memcpy(buf + 0x800, "boot-recovery", 13);
    memcpy(buf + 0x840, "recovery\n--rk_fwupdate\n", 23);
    return 0;
}

int rk_misc_build_clean(uint8_t *buf, size_t buf_len)
{
    if (buf_len < 0x2000) return -1;
    memset(buf, 0, 0x2000);
    return 0;
}
