#ifndef RK_DISK_H
#define RK_DISK_H

#include "rk_types.h"

struct rk_disk_info {
    char   path[512];       /* OS-specific device path */
    char   model[128];
    char   name[256];
    uint64_t size_bytes;
    int    removable;
};

struct rk_disk;

int  rk_disk_list(struct rk_disk_info *out, size_t max, size_t *n);

struct rk_disk *rk_disk_open(const char *path);
struct rk_disk *rk_disk_open_image(const char *path, uint64_t create_size);
int  rk_disk_close(struct rk_disk *d);

uint64_t rk_disk_size(struct rk_disk *d);

int  rk_disk_write(struct rk_disk *d, uint64_t offset,
                   const void *buf, size_t len);
int  rk_disk_read(struct rk_disk *d, uint64_t offset,
                  void *buf, size_t len);

/* Zero `len` bytes starting at `offset`. */
int  rk_disk_zero(struct rk_disk *d, uint64_t offset, uint64_t len);

/* Force underlying data to storage media. */
int  rk_disk_sync(struct rk_disk *d);

/* On Windows: lock/dismount volumes sharing this physical disk before writing.
 * Other platforms: noop. */
int  rk_disk_lock_volumes(struct rk_disk *d);
int  rk_disk_release_volumes(struct rk_disk *d);

/* Kick the kernel / mount manager to re-read the partition table. */
int  rk_disk_rescan(struct rk_disk *d);

#endif
