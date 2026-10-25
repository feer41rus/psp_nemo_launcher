#include "ui.h"
#include "gfx.h"

void ui_stub_draw(const App *app, StrId title, StrId desc) {
    (void)app;
    const char *t = tr(title), *d = tr(desc);
    int cy = SCREEN_H / 2 - 20;
    gfx_round_rect(SCREEN_W / 2 - 22, cy - 34, 44, 44, 12, COL_PANEL);
    gfx_button_glyph(GLYPH_NOTE, SCREEN_W / 2, cy - 12, 22);
    gfx_text(FONT_TITLE, (SCREEN_W - gfx_text_width(FONT_TITLE, t)) / 2, cy + 18, COL_TEXT, t);
    gfx_text(FONT_SMALL, (SCREEN_W - gfx_text_width(FONT_SMALL, d)) / 2, cy + 46, COL_MUTED, d);
}
