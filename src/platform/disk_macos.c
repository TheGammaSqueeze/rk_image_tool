#include "../common/disk.h"
#include "../common/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

struct rk_disk {
    int      fd;
    uint64_t size;
    char     path[256];
    int      is_image;
};

static uint64_t probe_size(int fd)
{
    uint64_t blk = 0, cnt = 0;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &blk) == 0 &&
        ioctl(fd, DKIOCGETBLOCKCOUNT, &cnt) == 0 && blk && cnt)
        return blk * cnt;
    off_t e = lseek(fd, 0, SEEK_END);
    if (e > 0) { lseek(fd, 0, SEEK_SET); return (uint64_t)e; }
    return 0;
}

int rk_disk_list(struct rk_disk_info *out, size_t max, size_t *n)
{
    *n = 0;
    /* macOS raw disks are /dev/rdiskN (character device) */
    for (int i = 0; i < 16; ++i) {
        char raw[64], blk[64];
        snprintf(raw, sizeof(raw), "/dev/rdisk%d", i);
        snprintf(blk, sizeof(blk), "/dev/disk%d", i);
        struct stat st;
        if (stat(raw, &st) != 0) continue;
        int fd = open(raw, O_RDONLY);
        if (fd < 0) continue;
        uint64_t sz = probe_size(fd);
        close(fd);
        if (!sz) continue;
        struct rk_disk_info di;
        memset(&di, 0, sizeof(di));
        snprintf(di.path, sizeof(di.path), "%s", raw);
        snprintf(di.name, sizeof(di.name), "disk%d", i);
        di.size_bytes = sz;
        di.removable = 1;
        if (*n < max) out[(*n)++] = di;
    }
    return 0;
}

struct rk_disk *rk_disk_open(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) { rk_err("open %s: %s\n", path, strerror(errno)); return NULL; }
    struct rk_disk *d = (struct rk_disk *)calloc(1, sizeof(*d));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    snprintf(d->path, sizeof(d->path), "%s", path);
    d->size = probe_size(fd);
    d->is_image = 0;
    return d;
}

struct rk_disk *rk_disk_open_image(const char *path, uint64_t create_size)
{
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { rk_err("open %s: %s\n", path, strerror(errno)); return NULL; }
    if (create_size && ftruncate(fd, (off_t)create_size) != 0) {
        close(fd); return NULL;
    }
    struct rk_disk *d = (struct rk_disk *)calloc(1, sizeof(*d));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    snprintf(d->path, sizeof(d->path), "%s", path);
    d->size = probe_size(fd);
    d->is_image = 1;
    return d;
}

int rk_disk_close(struct rk_disk *d)
{
    if (!d) return 0;
    if (d->fd >= 0) close(d->fd);
    free(d);
    return 0;
}

uint64_t rk_disk_size(struct rk_disk *d) { return d->size; }

int rk_disk_write(struct rk_disk *d, uint64_t offset,
                  const void *buf, size_t len)
{
    if (lseek(d->fd, (off_t)offset, SEEK_SET) < 0) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        ssize_t w = write(d->fd, p, len);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w; len -= (size_t)w;
    }
    return 0;
}

int rk_disk_read(struct rk_disk *d, uint64_t offset, void *buf, size_t len)
{
    if (lseek(d->fd, (off_t)offset, SEEK_SET) < 0) return -1;
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        ssize_t r = read(d->fd, p, len);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += r; len -= (size_t)r;
    }
    return 0;
}

int rk_disk_sync(struct rk_disk *d) { fsync(d->fd); return 0; }

int rk_disk_lock_volumes(struct rk_disk *d)
{
    /* Caller should `diskutil unmountDisk` themselves on macOS. */
    (void)d;
    return 0;
}
int rk_disk_release_volumes(struct rk_disk *d) { (void)d; return 0; }

int rk_disk_rescan(struct rk_disk *d) { (void)d; return 0; }
