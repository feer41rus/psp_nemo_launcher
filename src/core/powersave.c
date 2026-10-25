#include "powersave.h"

const int BACKLIGHT_CHOICES[6] = { 0, 10, 30, 60, 300, 600 };
const int SLEEP_CHOICES[7] = { 0, 60, 300, 600, 900, 1800, 3600 };

int powersave_choice_index(const int *choices, int n, int seconds) {
    int best = 0;
    for (int i = 1; i < n; i++) {
        int d = choices[i] - seconds, bd = choices[best] - seconds;
        if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = i;
    }
    return best;
}

void powersave_reset(PowerSave *p, uint32_t now_ms) {
    p->last_ms = p->busy_ms = now_ms;
    p->off = p->slept = 0;
}

int powersave_press(PowerSave *p, uint32_t now_ms) {
    p->last_ms = now_ms;
    p->slept = 0;
    if (!p->off) return 0;
    p->off = 0;
    return 1;
}

PowerSaveOut powersave_update(PowerSave *p, uint32_t now_ms, int backlight_s, int sleep_s, int busy, int own_screen) {
    PowerSaveOut o = { 0, 0 };
    if (own_screen) p->off = 0;
    else if (!p->off && backlight_s > 0 && now_ms - p->last_ms >= (uint32_t)backlight_s * 1000u) p->off = 1;
    o.screen_off = p->off;
    if (busy) {
        p->busy_ms = now_ms;
        return o;
    }
    uint32_t since = now_ms - p->last_ms, idle_busy = now_ms - p->busy_ms;
    if (idle_busy < since) since = idle_busy;
    if (!p->slept && sleep_s > 0 && since >= (uint32_t)sleep_s * 1000u) {
        p->slept = 1;
        o.sleep_now = 1;
    }
    return o;
}
