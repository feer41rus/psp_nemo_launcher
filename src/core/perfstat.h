#ifndef NEMO_PERFSTAT_H
#define NEMO_PERFSTAT_H

#include <stddef.h>
#include <stdint.h>

// Share of frame time Nemo itself spends (not the whole system: the PSP does not report that), for the
// debug box (1.2.0). Recomputed every PERFSTAT_WINDOW_MS so the number is readable and cheap.
#define PERFSTAT_WINDOW_MS 500

typedef struct {
    uint32_t win_start, busy_ms, frame_ms;
    int load_pct;
} PerfStat;

void perfstat_init(PerfStat *p, uint32_t now_ms);
// busy = time from frame start until just before present/vsync; frame = full frame time.
void perfstat_frame(PerfStat *p, uint32_t now_ms, uint32_t busy_ms, uint32_t frame_ms);
int  perfstat_load(const PerfStat *p);   // last published %, 0..100; 0 before the first window ends
// Bytes as megabytes with one decimal: "18,4" (Russian) / "18.4" (English).
void perfstat_format_mb(char *out, size_t cap, uint32_t bytes, int lang_en);

#endif
