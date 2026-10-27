#include "batmodel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEARN_MU 0.3f          // share of the error corrected per sample (newer samples weigh more)
#define WIN_DROP 3             // close a window after 3 %
#define WIN_SLOW_MS (60u * 60000u)    // or after an hour with at least 1 %
#define WIN_GIVEUP_MS (180u * 60000u) // nothing in 3 hours: start over

void batmodel_default(BatModel *m) {
    // From the two tests on a PSP-3000 (2026-10-27), both at brightness level 4: standard (full load)
    // 0.6 %/min, light (~12 % load at 333 MHz) 0.36 %/min. The base / brightness split is still a guess;
    // learning corrects it.
    m->base = 0.1773f;
    m->k_cpu = 0.2727f;
    m->k_bright = 0.0375f;
    m->samples = 0;
}

float batmodel_cpu(int mhz, int load_pct) {
    if (mhz <= 0 || load_pct <= 0) return 0.0f;
    float c = (float)mhz / 333.0f * (float)load_pct / 100.0f;
    return c > 1.0f ? 1.0f : c;
}

float batmodel_rate(const BatModel *m, float cpu, int level) {
    return m->base + m->k_cpu * cpu + m->k_bright * (float)level;
}

int batmodel_minutes(const BatModel *m, int pct, float cpu, int level) {
    if (pct < 0) return -1;
    float r = batmodel_rate(m, cpu, level);
    if (r < 0.01f) return -1;
    return (int)lroundf((float)pct / r);
}

// Normalised LMS step toward the measured rate; coefficients stay >= 0.
void batmodel_learn(BatModel *m, float cpu, int level, float rate) {
    float x[3] = { 1.0f, cpu, (float)level }, *w[3] = { &m->base, &m->k_cpu, &m->k_bright };
    float e = rate - batmodel_rate(m, cpu, level), nn = x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    for (int i = 0; i < 3; i++) {
        *w[i] += LEARN_MU * e * x[i] / nn;
        if (*w[i] < 0.0f) *w[i] = 0.0f;
    }
    m->samples++;
}

// Fixed point (1/100000) so the file does not depend on float printing.
int batmodel_format(char *out, size_t cap, const BatModel *m) {
    return snprintf(out, cap, "base=%ld\nk_cpu=%ld\nk_bright=%ld\nsamples=%d\n", lroundf(m->base * 100000.0f),
                    lroundf(m->k_cpu * 100000.0f), lroundf(m->k_bright * 100000.0f), m->samples);
}

static int key(const char *text, const char *k, long *out) {
    const char *p = strstr(text, k);
    if (!p) return -1;
    char *end;
    *out = strtol(p + strlen(k), &end, 10);
    return end == p + strlen(k) ? -1 : 0;
}

int batmodel_parse(const char *text, BatModel *m) {
    long b, c, l, s;
    if (key(text, "base=", &b) || key(text, "k_cpu=", &c) || key(text, "k_bright=", &l) || key(text, "samples=", &s))
        return -1;
    m->base = (float)b / 100000.0f;
    m->k_cpu = (float)c / 100000.0f;
    m->k_bright = (float)l / 100000.0f;
    m->samples = (int)s;
    return 0;
}

void batwin_reset(BatWindow *w) {
    memset(w, 0, sizeof *w);
    w->armed_pct = -1;
}

static void arm(BatWindow *w, uint32_t now_ms, int pct) {
    float last = w->last_rate;
    batwin_reset(w);
    w->armed_pct = pct;
    w->prev_pct = pct;
    w->prev_ms = now_ms;
    w->last_rate = last;
}

static void begin(BatWindow *w, uint32_t now_ms, int pct) {
    w->started = 1;
    w->pct0 = w->prev_pct = pct;
    w->start_ms = w->edge_ms = w->prev_ms = now_ms;
    w->cpu_sum = w->level_sum = 0;
    w->n = 0;
}

static void learn(BatWindow *w, BatModel *m, float rate) {
    float cpu = w->n ? (float)(w->cpu_sum / w->n) : 0.0f;
    int level = w->n ? (int)lround(w->level_sum / w->n) : 0;
    batmodel_learn(m, cpu, level, rate);
    w->last_rate = rate;
}

int batwin_add(BatWindow *w, BatModel *m, uint32_t now_ms, int pct, int charging, float cpu, int level) {
    if (charging || pct < 0) { batwin_reset(w); return 0; }
    if (w->armed_pct < 0 && !w->started) { arm(w, now_ms, pct); return 0; }
    int prev = w->prev_pct;
    float dmin = (float)(now_ms - w->prev_ms) / 60000.0f;
    // A rise, or a fall faster than 3 %/min (plus one step of rounding): not a real drain.
    if (pct > prev || prev - pct > 1 + (int)(3.0f * dmin)) { arm(w, now_ms, pct); return 0; }
    w->prev_pct = pct;
    w->prev_ms = now_ms;
    if (!w->started) {
        if (pct < w->armed_pct) begin(w, now_ms, pct);
        return 0;
    }
    w->cpu_sum += cpu;
    w->level_sum += level;
    w->n++;
    if (pct < prev) w->edge_ms = now_ms;
    int drop = w->pct0 - pct;
    uint32_t span = now_ms - w->start_ms;
    if (drop >= WIN_DROP && pct < prev) {
        learn(w, m, (float)drop / ((float)span / 60000.0f));
        begin(w, now_ms, pct);
        return 1;
    }
    if (span >= WIN_SLOW_MS && drop >= 1) {
        learn(w, m, (float)drop / ((float)(w->edge_ms - w->start_ms) / 60000.0f));
        arm(w, now_ms, pct);         // wait for the next edge to stay edge-aligned
        return 1;
    }
    if (span >= WIN_GIVEUP_MS) arm(w, now_ms, pct);
    return 0;
}
