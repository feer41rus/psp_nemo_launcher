#include "ui.h"
#include "gfx.h"

#define PW_W 220
#define PW_ROW_H 24

static StrId item_label(PowerItem it) {
    switch (it) {
    case PI_SLEEP: return S_PWR_SLEEP;
    case PI_REBOOT: return S_PWR_REBOOT;
    case PI_SHUTDOWN: return S_PWR_SHUTDOWN;
    case PI_EXIT_XMB: return S_PWR_EXIT_XMB;
    default: return S_PWR_CLOSE;
    }
}

// Centered window over whatever is on screen (the screen below stays as it was).
void ui_power_draw(const App *a) {
    const PowerMenu *m = &a->power;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H - HINT_BAR_H, 0x000000a0);
    int h = 34 + m->count * PW_ROW_H + 8;
    int x = (SCREEN_W - PW_W) / 2, y = (SCREEN_H - HINT_BAR_H - h) / 2;
    gfx_round_rect(x, y, PW_W, h, 10, 0x1a2230f8);
    gfx_round_rect_outline(x, y, PW_W, h, 10, 0xffffff20);
    gfx_text(FONT_BOLD, x + 14, y + 9, COL_TEXT, tr(S_PWR_TITLE));
    for (int i = 0; i < m->count; i++) {
        int ry = y + 34 + i * PW_ROW_H;
        if (i == m->sel) gfx_round_rect(x + 6, ry, PW_W - 12, PW_ROW_H - 2, 6, 0x2b5fa8ff);
        gfx_text(FONT_BODY, x + 16, ry + 3, m->items[i] == PI_CLOSE ? COL_MUTED : COL_TEXT, tr(item_label(m->items[i])));
    }
    static const Hint hints[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
    ui_hints_draw(a, hints, 2);
}

// While HOME is held: a small bar above the hint bar on the right, full at the moment the window opens.
void ui_home_bar_draw(const App *a) {
    int p = homekey_progress(&a->home_key, a->now_ms);
    if (p <= 0 || a->net_screen == NETS_USB) return;   // the window does not open there
    const char *t = tr(S_PWR_HOLD);
    int w = 120, h = 22, x = SCREEN_W - w - 8, y = SCREEN_H - HINT_BAR_H - h - 6;
    gfx_round_rect(x, y, w, h, 7, 0x1a2230f0);
    gfx_text(FONT_SMALL, x + 8, y + 3, COL_TEXT, t);
    gfx_round_rect(x + 8, y + h - 6, w - 16, 3, 1, 0x3a4656ff);
    gfx_round_rect(x + 8, y + h - 6, (w - 16) * p / 100, 3, 1, COL_ACCENT);
}
