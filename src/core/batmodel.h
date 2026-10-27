#ifndef NEMO_BATMODEL_H
#define NEMO_BATMODEL_H

#include <stddef.h>
#include <stdint.h>

// Nemo's own battery time estimate (1.2.2): drain in % per minute from CPU load and brightness,
//   rate = base + k_cpu * cpu + k_bright * level
// cpu = clock / 333 * load (0..1); level = brightness 1..4, 0 = backlight off. Starts from the standard
// and light tests (0.6 and 0.36 %/min at level 4) and learns from real use on battery. Used memory costs almost
// nothing by itself; memory traffic goes with CPU load, so it is in k_cpu.
typedef struct { float base, k_cpu, k_bright; int samples; } BatModel;

#define BATMODEL_SURE 5   // fewer learned samples: the estimate is marked "being refined"

void  batmodel_default(BatModel *m);
float batmodel_cpu(int mhz, int load_pct);
float batmodel_rate(const BatModel *m, float cpu, int level);
int   batmodel_minutes(const BatModel *m, int pct, float cpu, int level);   // -1 unknown
void  batmodel_learn(BatModel *m, float cpu, int level, float rate);
int   batmodel_format(char *out, size_t cap, const BatModel *m);
int   batmodel_parse(const char *text, BatModel *m);   // 0 ok, -1 a key is missing

// One measurement: from one change of the charge % to a later one (both edges, so whole percents are
// timed exactly), with the average load and brightness in between. Charging, a rising % and jumps
// (a false reading after a power cut: 97 -> 74 in 10 s) start it over.
typedef struct {
    int armed_pct;                // last % before the window starts; -1 none
    int started, pct0, prev_pct;
    uint32_t start_ms, edge_ms, prev_ms;
    double cpu_sum, level_sum;
    int n;
    float last_rate;              // last learned rate, %/min
} BatWindow;

void batwin_reset(BatWindow *w);
// Call about once a second. 1 = a sample was learned into m (save it).
int  batwin_add(BatWindow *w, BatModel *m, uint32_t now_ms, int pct, int charging, float cpu, int level);

#endif
