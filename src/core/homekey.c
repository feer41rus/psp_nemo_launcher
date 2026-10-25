#include "homekey.h"
#include "buttons.h"

HomeEvent homekey_update(HomeKey *k, uint32_t buttons, uint32_t *pressed, uint32_t now_ms) {
    int fresh = (*pressed & NB_HOME) != 0;
    *pressed &= ~(uint32_t)NB_HOME;
    if (!(buttons & NB_HOME)) {
        int tap = k->held && !k->fired;
        k->held = k->fired = 0;
        return tap ? HOME_TAP : HOME_NONE;
    }
    if (fresh && !k->held) {
        k->held = 1;
        k->fired = 0;
        k->down_ms = now_ms;
    }
    if (k->held && !k->fired && now_ms - k->down_ms >= HOME_HOLD_MS) {
        k->fired = 1;
        return HOME_HOLD;
    }
    return HOME_NONE;
}

int homekey_progress(const HomeKey *k, uint32_t now_ms) {
    if (!k->held || k->fired) return 0;
    uint32_t t = now_ms - k->down_ms;
    if (t <= HOME_BAR_DELAY_MS) return 0;
    if (t >= HOME_HOLD_MS) return 100;
    return (int)((t - HOME_BAR_DELAY_MS) * 100 / (HOME_HOLD_MS - HOME_BAR_DELAY_MS));
}
