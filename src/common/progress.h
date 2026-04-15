#ifndef RK_PROGRESS_H
#define RK_PROGRESS_H

#include "rk_types.h"

struct rk_progress;

struct rk_progress *rk_progress_start(const char *label, uint64_t total);
void rk_progress_update(struct rk_progress *p, uint64_t done);
void rk_progress_add(struct rk_progress *p, uint64_t delta);
void rk_progress_finish(struct rk_progress *p);

void rk_progress_set_enabled(int enabled);

#endif
