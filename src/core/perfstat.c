#include "perfstat.h"
#include <stdio.h>

void perfstat_init(PerfStat *p, uint32_t now_ms) {
    p->win_start = now_ms;
    p->busy_ms = p->frame_ms = 0;
    p->load_pct = 0;
}

void perfstat_frame(PerfStat *p, uint32_t now_ms, uint32_t busy_ms, uint32_t frame_ms) {
    p->busy_ms += busy_ms;
    p->frame_ms += frame_ms;
    if (now_ms - p->win_start < PERFSTAT_WINDOW_MS) return;
    int pct = p->frame_ms ? (int)((uint64_t)p->busy_ms * 100 / p->frame_ms) : 0;
    p->load_pct = pct > 100 ? 100 : pct;
    p->win_start = now_ms;
    p->busy_ms = p->frame_ms = 0;
}

int perfstat_load(const PerfStat *p) { return p->load_pct; }

void perfstat_format_mb(char *out, size_t cap, uint32_t bytes, int lang_en) {
    uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10 + 524288) / 1048576);
    snprintf(out, cap, "%u%c%u", (unsigned)(tenths / 10), lang_en ? '.' : ',', (unsigned)(tenths % 10));
}
