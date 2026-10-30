#include "scrstat.h"
#include <stdio.h>
#include <string.h>

void scrstat_begin(ScrStat *s, const char *name, uint32_t now_ms, uint32_t ram_free) {
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->start_ms = now_ms;
    s->fps_min = -1;
    s->ram_start = ram_free;
}

int scrstat_same(const ScrStat *s, const char *name) { return strncmp(s->name, name, sizeof s->name - 1) == 0; }

int scrstat_frame(ScrStat *s, uint32_t frame_ms) {
    s->frames++;
    if (frame_ms > s->worst_ms) s->worst_ms = frame_ms;
    if (frame_ms < SCRSTAT_SLOW_MS) return 0;
    s->slow++;
    return 1;
}

void scrstat_second(ScrStat *s, int fps) {
    if (s->fps_min < 0 || fps < s->fps_min) s->fps_min = fps;
}

int scrstat_summary(const ScrStat *s, uint32_t now_ms, uint32_t ram_free, char *out, size_t cap) {
    uint32_t dur = now_ms - s->start_ms;
    if (dur < SCRSTAT_MIN_VISIT_MS && !s->slow) return 0;
    char min[12];
    if (s->fps_min < 0) snprintf(min, sizeof min, "-");
    else snprintf(min, sizeof min, "%d", s->fps_min);
    unsigned avg = dur ? (unsigned)((uint64_t)s->frames * 1000 / dur) : 0;
    unsigned kb0 = s->ram_start / 1024, kb1 = ram_free / 1024;
    snprintf(out, cap, "screen: %s %u.%u s, fps avg %u min %s, worst %u ms, slow %u, ram_free %u->%u KB (%+d)", s->name,
             (unsigned)(dur / 1000), (unsigned)(dur % 1000 / 100), avg, min, (unsigned)s->worst_ms, (unsigned)s->slow,
             kb0, kb1, (int)kb1 - (int)kb0);
    return 1;
}
