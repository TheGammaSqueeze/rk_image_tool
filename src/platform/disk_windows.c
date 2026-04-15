#include "../common/disk.h"
#include "../common/util.h"

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rk_disk {
    HANDLE   h;
    uint64_t size;
    char     path[256];
    HANDLE   vol_handles[26];
    int      vol_count;
    int      is_image;
};

static uint64_t probe_size(HANDLE h)
{
    GET_LENGTH_INFORMATION li;
    DWORD ret = 0;
    if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                        &li, sizeof(li), &ret, NULL))
        return (uint64_t)li.Length.QuadPart;
    LARGE_INTEGER z = { .QuadPart = 0 };
    LARGE_INTEGER p;
    if (SetFilePointerEx(h, z, NULL, FILE_BEGIN) &&
        GetFileSizeEx(h, &p))
        return (uint64_t)p.QuadPart;
    return 0;
}

int rk_disk_list(struct rk_disk_info *out, size_t max, size_t *n)
{
    *n = 0;
    for (int i = 0; i < 32; ++i) {
        char path[64];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        struct rk_disk_info di = {0};
        di.size_bytes = probe_size(h);
        STORAGE_PROPERTY_QUERY q = { .PropertyId = StorageDeviceProperty,
                                     .QueryType = PropertyStandardQuery };
        STORAGE_DEVICE_DESCRIPTOR desc = {0};
        DWORD ret = 0;
        DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                        &desc, sizeof(desc), &ret, NULL);
        di.removable = (desc.RemovableMedia != 0);
        _snprintf_s(di.path, sizeof(di.path), _TRUNCATE, "%s", path);
        _snprintf_s(di.name, sizeof(di.name), _TRUNCATE, "PhysicalDrive%d", i);
        CloseHandle(h);
        if (di.size_bytes && *n < max) out[(*n)++] = di;
    }
    return 0;
}

static HANDLE open_raw(const char *path)
{
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
}

struct rk_disk *rk_disk_open(const char *path)
{
    HANDLE h = open_raw(path);
    if (h == INVALID_HANDLE_VALUE) {
        rk_err("CreateFile %s failed: %lu\n", path, GetLastError());
        return NULL;
    }
    struct rk_disk *d = (struct rk_disk *)calloc(1, sizeof(*d));
    if (!d) { CloseHandle(h); return NULL; }
    d->h = h;
    _snprintf_s(d->path, sizeof(d->path), _TRUNCATE, "%s", path);
    d->size = probe_size(h);
    d->is_image = 0;
    return d;
}

struct rk_disk *rk_disk_open_image(const char *path, uint64_t create_size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        rk_err("CreateFile %s failed: %lu\n", path, GetLastError());
        return NULL;
    }
    if (create_size) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)create_size;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
    }
    struct rk_disk *d = (struct rk_disk *)calloc(1, sizeof(*d));
    if (!d) { CloseHandle(h); return NULL; }
    d->h = h;
    _snprintf_s(d->path, sizeof(d->path), _TRUNCATE, "%s", path);
    d->size = probe_size(h);
    d->is_image = 1;
    return d;
}

int rk_disk_close(struct rk_disk *d)
{
    if (!d) return 0;
    rk_disk_release_volumes(d);
    if (d->h && d->h != INVALID_HANDLE_VALUE) CloseHandle(d->h);
    free(d);
    return 0;
}

uint64_t rk_disk_size(struct rk_disk *d) { return d->size; }

int rk_disk_write(struct rk_disk *d, uint64_t offset,
                  const void *buf, size_t len)
{
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(d->h, li, NULL, FILE_BEGIN)) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        DWORD w = 0, chunk = len > 0x100000 ? 0x100000 : (DWORD)len;
        if (!WriteFile(d->h, p, chunk, &w, NULL)) {
            rk_err("WriteFile: %lu\n", GetLastError());
            return -1;
        }
        p += w; len -= w;
    }
    return 0;
}

int rk_disk_read(struct rk_disk *d, uint64_t offset, void *buf, size_t len)
{
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(d->h, li, NULL, FILE_BEGIN)) return -1;
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        DWORD r = 0, chunk = len > 0x100000 ? 0x100000 : (DWORD)len;
        if (!ReadFile(d->h, p, chunk, &r, NULL) || r == 0) return -1;
        p += r; len -= r;
    }
    return 0;
}

int rk_disk_sync(struct rk_disk *d) { FlushFileBuffers(d->h); return 0; }

int rk_disk_lock_volumes(struct rk_disk *d)
{
    if (d->is_image) return 0;
    int physidx = -1;
    sscanf_s(d->path, "\\\\.\\PhysicalDrive%d", &physidx);
    if (physidx < 0) return 0;

    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        char root[8];
        _snprintf_s(root, sizeof(root), _TRUNCATE, "%c:\\", 'A' + i);
        if (GetDriveTypeA(root) != DRIVE_REMOVABLE) continue;
        char vol[16];
        _snprintf_s(vol, sizeof(vol), _TRUNCATE, "\\\\.\\%c:", 'A' + i);
        HANDLE h = CreateFileA(vol, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        VOLUME_DISK_EXTENTS ext = {0};
        DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                            &ext, sizeof(ext), &ret, NULL) &&
            ext.NumberOfDiskExtents >= 1 &&
            (int)ext.Extents[0].DiskNumber == physidx) {
            DWORD dummy = 0;
            DeviceIoControl(h, FSCTL_LOCK_VOLUME,    NULL, 0, NULL, 0, &dummy, NULL);
            DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME,NULL, 0, NULL, 0, &dummy, NULL);
            if (d->vol_count < 26) d->vol_handles[d->vol_count++] = h;
            continue;
        }
        CloseHandle(h);
    }
    return 0;
}

int rk_disk_release_volumes(struct rk_disk *d)
{
    for (int i = 0; i < d->vol_count; ++i) {
        DWORD dummy = 0;
        DeviceIoControl(d->vol_handles[i], FSCTL_UNLOCK_VOLUME,
                        NULL, 0, NULL, 0, &dummy, NULL);
        CloseHandle(d->vol_handles[i]);
    }
    d->vol_count = 0;
    return 0;
}

int rk_disk_rescan(struct rk_disk *d)
{
    if (d->is_image) return 0;
    DWORD dummy = 0;
    DeviceIoControl(d->h, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0,
                    &dummy, NULL);
    return 0;
}
