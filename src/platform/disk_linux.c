#include "../common/disk.h"
#include "../common/util.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#include <linux/fs.h>

struct rk_disk {
    int      fd;
    uint64_t size;
    char     path[256];
    int      is_image;
};

static uint64_t probe_size(int fd)
{
    uint64_t sz = 0;
#ifdef BLKGETSIZE64
    if (ioctl(fd, BLKGETSIZE64, &sz) == 0 && sz) return sz;
#endif
    off_t e = lseek(fd, 0, SEEK_END);
    if (e > 0) { lseek(fd, 0, SEEK_SET); return (uint64_t)e; }
    return 0;
}

static int read_sysfs_u64(const char *path, uint64_t *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int rc = (fscanf(fp, "%" SCNu64, out) == 1) ? 0 : -1;
    fclose(fp);
    return rc;
}
static int read_sysfs_line(const char *path, char *buf, size_t buflen)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(buf, (int)buflen, fp)) { fclose(fp); return -1; }
    fclose(fp);
    size_t n = strlen(buf);
    while (n && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        buf[--n] = 0;
    return 0;
}

int rk_disk_list(struct rk_disk_info *out, size_t max, size_t *n)
{
    *n = 0;
    DIR *dp = opendir("/sys/block");
    if (!dp) return -1;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (strncmp(de->d_name, "loop", 4) == 0) continue;
        if (strncmp(de->d_name, "ram", 3) == 0) continue;
        if (strncmp(de->d_name, "dm-", 3) == 0) continue;
        if (strncmp(de->d_name, "zram", 4) == 0) continue;

        char p[PATH_MAX];
        struct rk_disk_info di;
        memset(&di, 0, sizeof(di));

        snprintf(p, sizeof(p), "/sys/block/%s/size", de->d_name);
        uint64_t sectors = 0;
        if (read_sysfs_u64(p, &sectors) != 0) continue;
        di.size_bytes = sectors * 512ULL;
        if (di.size_bytes == 0) continue;

        snprintf(p, sizeof(p), "/sys/block/%s/removable", de->d_name);
        uint64_t removable = 0;
        read_sysfs_u64(p, &removable);
        di.removable = (int)removable;

        snprintf(p, sizeof(p), "/sys/block/%s/device/model", de->d_name);
        read_sysfs_line(p, di.model, sizeof(di.model));

        snprintf(di.path, sizeof(di.path), "/dev/%s", de->d_name);
        snprintf(di.name, sizeof(di.name), "%s", de->d_name);

        if (*n < max) out[(*n)++] = di;
    }
    closedir(dp);
    return 0;
}

struct rk_disk *rk_disk_open(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
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
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) { rk_err("open %s: %s\n", path, strerror(errno)); return NULL; }
    if (create_size) {
        if (ftruncate(fd, (off_t)create_size) != 0) {
            rk_err("truncate %s: %s\n", path, strerror(errno));
            close(fd);
            return NULL;
        }
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
    if (offset + len > d->size && !d->is_image) {
        rk_err("write past end of disk: %llu + %zu > %llu\n",
               (unsigned long long)offset, len, (unsigned long long)d->size);
        return -1;
    }
    if (lseek(d->fd, (off_t)offset, SEEK_SET) < 0) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        ssize_t w = write(d->fd, p, len);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            rk_err("write at %llu: %s\n",
                   (unsigned long long)offset, strerror(errno));
            return -1;
        }
        p += w; len -= (size_t)w; offset += (uint64_t)w;
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

int rk_disk_lock_volumes(struct rk_disk *d) { (void)d; return 0; }
int rk_disk_release_volumes(struct rk_disk *d) { (void)d; return 0; }

int rk_disk_rescan(struct rk_disk *d)
{
#ifdef BLKRRPART
    ioctl(d->fd, BLKRRPART);
#endif
    return 0;
}
