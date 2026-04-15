#ifndef RK_UTIL_H
#define RK_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

int  rk_path_exists(const char *path);
int  rk_mkdir_p(const char *path);
int  rk_join(char *out, size_t out_size, const char *a, const char *b);
int  rk_dirname(char *out, size_t out_size, const char *path);
uint64_t rk_file_size(FILE *fp);

int rk_read_all(const char *path, uint8_t **buf, uint64_t *len);
int rk_write_all(const char *path, const void *buf, uint64_t len);
int rk_copy_file(const char *src, const char *dst);

void rk_log(const char *fmt, ...);
void rk_err(const char *fmt, ...);
void rk_set_verbose(int v);
int  rk_verbose(void);

#endif
