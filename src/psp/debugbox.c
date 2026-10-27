#include "ui.h"
#include "gfx.h"
#include <stdio.h>

#define DBG_LINE_H 16   // the old debug line (main.c draw_debug) sits right above the hint bar

void ui_debugbox_draw(const App *a) {
    const NemoSettings *s = &a->lib->settings;
    if (!a->debug || !(s->dbg_ram || s->dbg_cpu || s->dbg_fps)) return;
    char rows[3][48];
    int n = 0, en = i18n_get() == LANG_EN;
    if (s->dbg_ram) {
        char fr[12], tot[12];
        perfstat_format_mb(fr, sizeof fr, a->ram_free, en);
        if (a->ram_total) {
            perfstat_format_mb(tot, sizeof tot, a->ram_total, en);
            snprintf(rows[n++], sizeof rows[0], tr(S_DBG_RAM_FMT), fr, tot);
        } else {
            snprintf(rows[n++], sizeof rows[0], tr(S_DBG_RAM_FREE_FMT), fr);
        }
    }
    if (s->dbg_cpu) snprintf(rows[n++], sizeof rows[0], tr(S_DBG_CPU_FMT), a->cpu_mhz, perfstat_load(&a->perf));
    if (s->dbg_fps) snprintf(rows[n++], sizeof rows[0], "FPS %d", a->fps);
    int lh = gfx_text_height(FONT_SMALL), w = 0;
    for (int i = 0; i < n; i++) {
        int tw = gfx_text_width(FONT_SMALL, rows[i]);
        if (tw > w) w = tw;
    }
    int h = n * lh + 8, x = 4, y = SCREEN_H - HINT_BAR_H - DBG_LINE_H - 4 - h;
    gfx_round_rect(x, y, w + 12, h, 6, 0x000000b0);
    for (int i = 0; i < n; i++) gfx_text(FONT_SMALL, x + 6, y + 4 + i * lh, 0x5fe0b0ff, rows[i]);
}
