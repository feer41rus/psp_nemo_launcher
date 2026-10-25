#include "powermenu.h"

void powermenu_open(PowerMenu *m, int show_exit_xmb) {
    m->count = 0;
    m->items[m->count++] = PI_SLEEP;
    m->items[m->count++] = PI_REBOOT;
    m->items[m->count++] = PI_SHUTDOWN;
    if (show_exit_xmb) m->items[m->count++] = PI_EXIT_XMB;
    m->items[m->count++] = PI_CLOSE;
    m->sel = 0;
    m->open = 1;
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
