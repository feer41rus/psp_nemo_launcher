#include "sleeptimer.h"

#define FADE_MS 60000u

static int timed(SleepChoice c) { return c >= SLEEP_15 && c <= SLEEP_60; }

void sleep_set(SleepTimer *t, SleepChoice c, uint32_t now_ms) {
    static const uint32_t minutes[SLEEP_COUNT] = { 0, 15, 30, 45, 60, 0 };
    t->choice = c;
    t->deadline_ms = timed(c) ? now_ms + minutes[c] * 60000u : 0;
}

SleepChoice sleep_step(SleepChoice c, int dir) {
    int n = ((int)c + (dir < 0 ? SLEEP_COUNT - 1 : 1)) % SLEEP_COUNT;
    return (SleepChoice)n;
}

uint32_t sleep_remaining_ms(const SleepTimer *t, uint32_t now_ms) {
    if (!timed(t->choice)) return 0;
    int32_t left = (int32_t)(t->deadline_ms - now_ms);
    return left > 0 ? (uint32_t)left : 0;
}

int sleep_fade_percent(const SleepTimer *t, uint32_t now_ms) {
    if (!timed(t->choice)) return 100;
    uint32_t left = sleep_remaining_ms(t, now_ms);
    if (left >= FADE_MS) return 100;
    return (int)(left * 100 / FADE_MS);
}

int sleep_due(const SleepTimer *t, uint32_t now_ms) {
    return timed(t->choice) && (int32_t)(now_ms - t->deadline_ms) >= 0;
}
