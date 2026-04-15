#ifndef RK_TYPES_H
#define RK_TYPES_H

#include <stdint.h>
#include <stddef.h>

#if defined(_MSC_VER)
#define RK_PACKED_BEGIN __pragma(pack(push, 1))
#define RK_PACKED_END   __pragma(pack(pop))
#define RK_PACKED
#else
#define RK_PACKED_BEGIN
#define RK_PACKED_END
#define RK_PACKED __attribute__((packed))
#endif

#define RK_SECTOR_SIZE       512u
#define RK_IDB_SECTOR        64u
#define RK_IDB_BLOCK_SIZE    0x200u

#endif
