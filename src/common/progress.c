#include "progress.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct rk_progress {
    char label[96];
    uint64_t total;
    uint64_t done;
    time_t last_print;
    int finished;
};

static int g_progress_enabled = 1;

void rk_progress_set_enabled(int enabled) { g_progress_enabled = enabled ? 1 : 0; }

static int should_print(struct rk_progress *p)
{
    time_t now = time(NULL);
    if (now != p->last_print || p->done == p->total) {
        p->last_print = now;
        return 1;
    }
    return 0;
}

static void human_bytes(uint64_t n, char *out, size_t out_sz)
{
    static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    int u = 0;
    double v = (double)n;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    snprintf(out, out_sz, "%.2f %s", v, unit[u]);
}

static void render(struct rk_progress *p)
{
    if (!g_progress_enabled) return;
    int pct = p->total ? (int)((p->done * 100) / p->total) : 0;
    if (pct > 100) pct = 100;
    char d[32], t[32];
    human_bytes(p->done, d, sizeof(d));
    human_bytes(p->total, t, sizeof(t));

    char bar[41];
    int filled = pct * 40 / 100;
    for (int i = 0; i < 40; ++i) bar[i] = i < filled ? '#' : '-';
    bar[40] = 0;

    fprintf(stderr, "\r%-24s [%s] %3d%% %s / %s    ",
            p->label, bar, pct, d, t);
    fflush(stderr);
}

struct rk_progress *rk_progress_start(const char *label, uint64_t total)
{
    struct rk_progress *p = (struct rk_progress *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    snprintf(p->label, sizeof(p->label), "%s", label ? label : "");
    p->total = total;
    p->done = 0;
    p->last_print = 0;
    render(p);
    return p;
}

void rk_progress_update(struct rk_progress *p, uint64_t done)
{
    if (!p) return;
    p->done = done;
    if (should_print(p)) render(p);
}

void rk_progress_add(struct rk_progress *p, uint64_t delta)
{
    if (!p) return;
    p->done += delta;
    if (should_print(p)) render(p);
}

void rk_progress_finish(struct rk_progress *p)
{
    if (!p || p->finished) { free(p); return; }
    p->finished = 1;
    p->done = p->total;
    render(p);
    if (g_progress_enabled) fprintf(stderr, "\n");
    free(p);
}
