#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define PATH_SEP '\\'
#define PATH_SEP_ALT '/'
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#define PATH_SEP '/'
#define PATH_SEP_ALT '\\'
#endif

static int g_verbose = 1;

void rk_set_verbose(int v) { g_verbose = v; }
int  rk_verbose(void)      { return g_verbose; }

void rk_log(const char *fmt, ...)
{
    if (!g_verbose) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void rk_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

int rk_path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int rk_mkdir_p(const char *path)
{
    if (!path || !*path) return -1;
    size_t n = strlen(path);
    char *buf = (char *)malloc(n + 2);
    if (!buf) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; ++i) {
        if (buf[i] == PATH_SEP || buf[i] == PATH_SEP_ALT) {
            char c = buf[i];
            buf[i] = 0;
            if (!rk_path_exists(buf)) MKDIR(buf);
            buf[i] = c;
        }
    }
    if (!rk_path_exists(buf)) MKDIR(buf);
    free(buf);
    return 0;
}

int rk_join(char *out, size_t out_size, const char *a, const char *b)
{
    if (!out || out_size == 0) return -1;
    size_t la = a ? strlen(a) : 0;
    int need_sep = la > 0 && a[la-1] != PATH_SEP && a[la-1] != PATH_SEP_ALT;
    size_t n = (size_t)snprintf(out, out_size, "%s%s%s",
                                a ? a : "",
                                need_sep ? (char[]){PATH_SEP,0} : "",
                                b ? b : "");
    return (n >= out_size) ? -1 : 0;
}

int rk_dirname(char *out, size_t out_size, const char *path)
{
    if (!out || !path || out_size == 0) return -1;
    size_t n = strlen(path);
    while (n > 0 && path[n-1] != PATH_SEP && path[n-1] != PATH_SEP_ALT) n--;
    if (n == 0) { if (out_size < 2) return -1; out[0]='.'; out[1]=0; return 0; }
    if (n >= out_size) return -1;
    memcpy(out, path, n - 1);
    out[n-1] = 0;
    return 0;
}

uint64_t rk_file_size(FILE *fp)
{
#if defined(_WIN32)
    long long cur = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_END);
    long long end = _ftelli64(fp);
    _fseeki64(fp, cur, SEEK_SET);
    return (uint64_t)end;
#else
    off_t cur = ftello(fp);
    fseeko(fp, 0, SEEK_END);
    off_t end = ftello(fp);
    fseeko(fp, cur, SEEK_SET);
    return (uint64_t)end;
#endif
}

int rk_read_all(const char *path, uint8_t **buf, uint64_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    uint64_t sz = rk_file_size(fp);
    uint8_t *b = (uint8_t *)malloc((size_t)sz);
    if (!b) { fclose(fp); return -1; }
    if (fread(b, 1, (size_t)sz, fp) != sz) { free(b); fclose(fp); return -1; }
    fclose(fp);
    *buf = b; *len = sz;
    return 0;
}

int rk_write_all(const char *path, const void *buf, uint64_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t w = fwrite(buf, 1, (size_t)len, fp);
    fclose(fp);
    return (w == len) ? 0 : -1;
}

int rk_copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    uint8_t buf[64*1024];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) { fclose(in); fclose(out); return -1; }
    }
    fclose(in); fclose(out);
    return 0;
}
