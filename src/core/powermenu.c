#include "powermenu.h"

void powermenu_open(PowerMenu *m, int show_exit_xmb, uint32_t now_ms) {
    m->count = 0;
    m->items[m->count++] = PI_SLEEP;
    m->items[m->count++] = PI_REBOOT;
    m->items[m->count++] = PI_SHUTDOWN;
    if (show_exit_xmb) m->items[m->count++] = PI_EXIT_XMB;
    m->items[m->count++] = PI_CLOSE;
    m->sel = 0;
    m->open = 1;
    m->opened_ms = now_ms;
}

int powermenu_dim_alpha(const PowerMenu *m, uint32_t now_ms) {
    if (!m->open) return 0;
    uint32_t t = now_ms - m->opened_ms;
    if (t >= POWER_DIM_FADE_MS) return POWER_DIM_ALPHA;
    return (int)(t * POWER_DIM_ALPHA / POWER_DIM_FADE_MS);
}

int powermenu_handle(PowerMenu *m, Action a) {
    switch (a) {
    case ACT_UP: if (m->sel > 0) m->sel--; return -1;
    case ACT_DOWN: if (m->sel < m->count - 1) m->sel++; return -1;
    case ACT_CONFIRM: m->open = 0; return m->items[m->sel];
    case ACT_BACK: case ACT_HOME: m->open = 0; return PI_CLOSE;
    default: return -1;
    }
}
