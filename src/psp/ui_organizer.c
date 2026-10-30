// Organizer tab (1.3.2): rows like the Settings menu. The Gallery and the file manager open as their
// own windows (everything else unloaded, the music keeps playing); Video (1.3.3) and Camera (1.3.6) too.
#include "ui.h"
#include "gfx.h"
#include "log.h"
#include "organizer.h"
#include <stdio.h>
#include <stdlib.h>

#define ROW_H 28
#define LIST_X 24
#define LIST_W (SCREEN_W - 48)
#define LIST_Y (CONTENT_Y + 12)

typedef struct OrgTab {
    int sel;
    int stub;            // the "coming soon" note is open
    int was_open;        // a window was open last frame
    uint32_t closed_ms;  // when the last window closed: Library artwork first, then Home
} OrgTab;

static OrgTab *org_state(App *a) {
    if (!a->org) a->org = calloc(1, sizeof *a->org);
    return a->org;
}

static int window_open(const App *a) {
    return photo_tab_owns_buttons(a) || files_is_open(a) || video_win_is_open(a) || cam_win_is_open(a);
}

int organizer_owns_buttons(const App *a) { return window_open(a) || (a->org && a->org->stub); }

int organizer_fullscreen(const App *a) {
    return a->nav.tab == TAB_ORGANIZER && (photo_tab_fullscreen(a) || files_is_open(a) || video_win_is_open(a) ||
                                         cam_win_is_open(a));
}

int organizer_busy(const App *a) { return photo_tab_busy(a) || files_busy(a) || cam_win_busy(a); }

static void open_row(App *a, OrgTab *s) {
    log_msg("organizer: open %s", tr_en(org_label((OrgItem)s->sel)));
    switch ((OrgItem)s->sel) {
    case ORG_GALLERY: photo_open(a); break;
    case ORG_FILES: files_open(a); break;
    case ORG_VIDEO: video_win_open(a); break;
    case ORG_CAMERA: cam_win_open(a); break;
    default: s->stub = 1; break;
    }
}

void organizer_action(App *a, Action act) {
    if (photo_tab_owns_buttons(a)) { photo_tab_action(a, act); return; }
    if (video_win_is_open(a)) { video_win_action(a, act); return; }
    if (cam_win_is_open(a)) { cam_win_action(a, act); return; }
    if (files_is_open(a)) { files_action(a, act); return; }
    OrgTab *s = org_state(a);
    if (!s) return;
    if (s->stub) {
        if (act == ACT_BACK || act == ACT_CONFIRM) s->stub = 0;
        return;
    }
    switch (act) {
    case ACT_UP: s->sel = org_move(s->sel, -1); break;
    case ACT_DOWN: s->sel = org_move(s->sel, 1); break;
    case ACT_CONFIRM: open_row(a, s); break;
    default: break;
    }
}

void organizer_tick(App *a) {
    photo_tab_tick(a);
    files_tick(a);
    video_win_tick(a);
    cam_win_tick(a);
    if (a->nav.tab != TAB_ORGANIZER) {
        if (a->org) a->org->stub = 0;
        return;
    }
    OrgTab *s = org_state(a);
    if (!s) return;
    int open = window_open(a);
    if (s->was_open && !open) s->closed_ms = a->now_ms;
    s->was_open = open;
    if (open) return;
    // The list: bring the menus' artwork back, Library first, then Home.
    shelves_preload(a, TAB_LIBRARY);
    if (a->now_ms - s->closed_ms > 1500) shelves_preload(a, TAB_HOME);
}

static void draw_stub(const App *a, const OrgTab *s) {
    const char *ver = org_soon_version((OrgItem)s->sel);
    char line[96];
    snprintf(line, sizeof line, tr(S_ORG_SOON_FMT), ver ? ver : "");
    int w = 300, h = 96, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000a0);
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, 0xffffff20);
    const char *title = tr(org_label((OrgItem)s->sel));
    gfx_text(FONT_BOLD, x + (w - gfx_text_width(FONT_BOLD, title)) / 2, y + 16, COL_TEXT, title);
    gfx_text(FONT_SMALL, x + (w - gfx_text_width(FONT_SMALL, tr(S_ORG_SOON))) / 2, y + 44, COL_ACCENT, tr(S_ORG_SOON));
    gfx_text_fit(FONT_SMALL, x + (w - gfx_text_width(FONT_SMALL, line)) / 2, y + 64, w - 20, COL_MUTED, line);
    (void)a;
}

void organizer_draw(const App *ac) {
    App *a = (App *)ac;
    if (photo_tab_owns_buttons(a)) { photo_tab_draw(a); return; }
    if (video_win_is_open(a)) { video_win_draw(a); return; }
    if (cam_win_is_open(a)) { cam_win_draw(a); return; }
    if (files_is_open(a)) { files_draw(a); return; }
    OrgTab *s = org_state(a);
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    if (!s) return;
    for (int i = 0; i < ORG_COUNT; i++) {
        int y = LIST_Y + i * ROW_H, sel = i == s->sel;
        int soon = org_soon_version((OrgItem)i) != NULL;
        gfx_round_rect(LIST_X, y, LIST_W, ROW_H - 4, 7, sel ? 0x24324aff : COL_PANEL);
        if (sel) gfx_round_rect_outline(LIST_X, y, LIST_W, ROW_H - 4, 7, COL_ACCENT);
        gfx_text_fit(FONT_BODY, LIST_X + 14, y + 5, LIST_W - 120, soon ? 0x5b6779ff : COL_TEXT, tr(org_label((OrgItem)i)));
        if (soon) {
            const char *t = tr(S_ORG_SOON);
            gfx_text(FONT_SMALL, LIST_X + LIST_W - 14 - gfx_text_width(FONT_SMALL, t), y + 6, 0x5b6779ff, t);
        }
    }
    if (s->stub) draw_stub(a, s);
}

void organizer_hints(const App *a) {
    if (photo_tab_owns_buttons(a)) { photo_tab_hints(a); return; }
    if (video_win_is_open(a)) { video_win_hints(a); return; }
    if (cam_win_is_open(a)) { cam_win_hints(a); return; }
    if (files_is_open(a)) { files_hints(a); return; }
    if (a->org && a->org->stub) {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
        return;
    }
    static const Hint h[] = { { ACT_CONFIRM, S_HINT_OPEN } };
    ui_hints_draw(a, h, 1);
}
