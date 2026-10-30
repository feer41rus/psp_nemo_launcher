#ifndef NEMO_SCRSTAT_H
#define NEMO_SCRSTAT_H

#include <stddef.h>
#include <stdint.h>

// Per-visit screen statistics for the log (1.2.6 analytics): how long a screen was open, average and
// lowest fps, the worst frame, slow frames and the free RAM on entry and exit (to spot leaks per screen).
#define SCRSTAT_SLOW_MS 100      // a frame this long is "slow" and gets its own log line
#define SCRSTAT_MIN_VISIT_MS 2000 // shorter visits without slow frames are not logged (menus passed through)

typedef struct {
    char name[48];
    uint32_t start_ms, frames, worst_ms, slow;
    int fps_min;                 // -1 until a whole second was measured
    uint32_t ram_start;          // bytes
} ScrStat;

void scrstat_begin(ScrStat *s, const char *name, uint32_t now_ms, uint32_t ram_free);
int  scrstat_same(const ScrStat *s, const char *name);   // 1 = still the same screen
int  scrstat_frame(ScrStat *s, uint32_t frame_ms);       // 1 = this frame was slow
void scrstat_second(ScrStat *s, int fps);                // once a second, the measured fps
// "screen: <name> 3.0 s, fps avg 30 min 31, worst 150 ms, slow 1, ram_free 10240->10180 KB (-60)";
// returns 0 (and no line) for a short visit without slow frames.
int  scrstat_summary(const ScrStat *s, uint32_t now_ms, uint32_t ram_free, char *out, size_t cap);

#endif
