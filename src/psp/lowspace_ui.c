// "Low on space" badge on Home (1.3.10): in Home's own space under the clock, Wi-Fi and battery, like the NCL
// logo below the shelves. On a PSP Go both storages are listed; the colour follows the fuller one.
#include "ui.h"
#include "gfx.h"
#include "gamesize.h"
#include "lowspace.h"
#include "memory_hw.h"
#include "platform.h"
#include "usb.h"
#include <stdio.h>

#define REFRESH_MS 30000   // free space changes slowly; devctl is a few ms, but not every frame
#define BADGE_BG 0x141b26bf   // 75 % opaque: the game's artwork shows through a little (author: 80 %, then 5 % more)

static struct {
    uint32_t at, seen;          // last read; last frame Home was drawn (coming back reads again)
    int read;
    LowSpace level;
    int lines;
    char line[2][48];
    int x, w, h;
} st;

static void refresh(App *a) {
    st.read = 1;
    st.at = a->now_ms;
    st.level = LOWSPACE_OK;
    st.lines = 0;
    if (usb_active()) return;   // the stick belongs to the PC
    int en = i18n_get() == LANG_EN, go = platform_model() == 4;
    MemCard ms, ef;
    int has_ms = memory_card_read("ms0:", &ms) == 0, has_ef = go && memory_card_read("ef0:", &ef) == 0;
    LowSpace lm = has_ms ? lowspace_level(ms.free, ms.total) : LOWSPACE_OK;
    LowSpace le = has_ef ? lowspace_level(ef.free, ef.total) : LOWSPACE_OK;
    st.level = lowspace_worst(lm, le);
    if (st.level == LOWSPACE_OK) return;
    char size[24];
    if (go) {                                  // both, so it is clear where games could go
        if (has_ef) {
            game_size_text(size, sizeof size, ef.free, en);
            snprintf(st.line[st.lines++], sizeof st.line[0], tr(S_LOWSPACE_INT_FMT), size);
        }
        if (has_ms) {
            game_size_text(size, sizeof size, ms.free, en);
            snprintf(st.line[st.lines++], sizeof st.line[0], tr(S_LOWSPACE_M2_FMT), size);
        }
    } else {
        game_size_text(size, sizeof size, ms.free, en);
        snprintf(st.line[st.lines++], sizeof st.line[0], tr(S_LOWSPACE_FREE_FMT), size);
    }
}

int lowspace_badge_x(App *a) {
    if (!a->lib->settings.low_space || a->nav.tab != TAB_HOME) return SCREEN_W;
    if (!st.read || a->now_ms - st.at >= REFRESH_MS || a->now_ms - st.seen > 1000) refresh(a);
    st.seen = a->now_ms;
    if (st.level == LOWSPACE_OK) return SCREEN_W;
    int tw = gfx_text_width(FONT_SMALL, tr(S_LOWSPACE_TITLE));
    for (int i = 0; i < st.lines; i++) {
        int lw = gfx_text_width(FONT_TINY, st.line[i]);
        if (lw > tw) tw = lw;
    }
    st.w = 23 + tw + 10;
    st.h = 8 + 14 + 11 * st.lines + 4;
    st.x = SCREEN_W - 8 - st.w;
    return st.x;
}

void lowspace_invalidate(void) { st.read = 0; }

// A memory card 7x9 with a "!" in it.
static void card_icon(int x, int y, uint32_t col) {
    gfx_rect(x, y, 5, 1, col);
    gfx_rect(x + 5, y + 1, 1, 1, col);
    gfx_rect(x + 6, y + 2, 1, 7, col);
    gfx_rect(x, y + 8, 6, 1, col);
    gfx_rect(x, y + 1, 1, 7, col);
    gfx_rect(x + 3, y + 2, 1, 4, col);
    gfx_rect(x + 3, y + 7, 1, 1, col);
}

void lowspace_draw(App *a) {
    if (lowspace_badge_x(a) >= SCREEN_W) return;
    uint32_t col = st.level == LOWSPACE_CRIT ? 0xff6f7dff : COL_WARN;
    int x = st.x, y = CONTENT_Y + 6;
    gfx_round_rect(x, y, st.w, st.h, 8, BADGE_BG);
    gfx_round_rect_outline(x, y, st.w, st.h, 8, (col & 0xffffff00u) | 0x90);
    card_icon(x + 10, y + 7, col);
    gfx_text(FONT_SMALL, x + 23, y + 4, col, tr(S_LOWSPACE_TITLE));
    for (int i = 0; i < st.lines; i++) gfx_text(FONT_TINY, x + 23, y + 18 + 11 * i, COL_MUTED, st.line[i]);
}
