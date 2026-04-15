#ifndef RK_SINK_H
#define RK_SINK_H

#include "rk_types.h"
#include <stdio.h>

struct rk_disk;
struct rk_sink;

struct rk_sink *rk_sink_open_disk(struct rk_disk *d);
struct rk_sink *rk_sink_open_image(const char *path, uint64_t create_size);
struct rk_sink *rk_sink_open_xz(const char *path, int preset);

struct rk_progress;

int  rk_sink_write(struct rk_sink *s, uint64_t offset, const void *buf, size_t len);
int  rk_sink_write_file(struct rk_sink *s, uint64_t offset,
                        const char *src_path, uint64_t src_off, uint64_t len);
int  rk_sink_write_file_progress(struct rk_sink *s, uint64_t offset,
                                 const char *src_path, uint64_t src_off,
                                 uint64_t len, struct rk_progress *pg);
int  rk_sink_zero(struct rk_sink *s, uint64_t offset, uint64_t len);
int  rk_sink_read(struct rk_sink *s, uint64_t offset, void *buf, size_t len);
int  rk_sink_set_size(struct rk_sink *s, uint64_t size);
uint64_t rk_sink_size(struct rk_sink *s);
uint64_t rk_sink_required_size(struct rk_sink *s);

int  rk_sink_supports_read(struct rk_sink *s);
int  rk_sink_sync(struct rk_sink *s);
int  rk_sink_close(struct rk_sink *s);

int  rk_path_has_suffix_xz(const char *path);

#endif
