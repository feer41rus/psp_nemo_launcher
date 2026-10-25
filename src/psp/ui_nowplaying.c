// Music: "Now Playing" (mockup A-2), the triangle menu, queue, playlist picker and hints.
#include "ui.h"
#include "art.h"
#include "gfx.h"
#include "player.h"
#include <stdio.h>
#include <string.h>

#define PANEL_W 250
#define ROW_H 22

static void fmt_time(uint32_t ms, char *out, size_t cap) {
    uint32_t s = ms / 1000;
    snprintf(out, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static const char *artist_of(const Track *t) {
    return t->artist[0] ? t->artist : tr(S_MUSIC_UNKNOWN_ARTIST);
}

// Transport icons (the font has no media symbols): kind 0 prev, 1 play, 2 pause, 3 next.
static void transport(int kind, int cx, int cy, int s, uint32_t col) {
    switch (kind) {
    case 0:   // |◀ : bar on the left, triangle pointing left
        for (int i = 0; i <= s; i++) gfx_rect(cx - s / 2 + i, cy - i, 1, i * 2 + 1, col);
        gfx_rect(cx - s / 2 - 3, cy - s, 2, s * 2 + 1, col);
        break;
    case 1:
        for (int i = 0; i <= s; i++) gfx_rect(cx - s / 2 + i, cy - s + i, 1, (s - i) * 2 + 1, col);
        break;
    case 2:
        gfx_rect(cx - s / 2 - 1, cy - s, 3, s * 2 + 1, col);
        gfx_rect(cx + s / 2 - 1, cy - s, 3, s * 2 + 1, col);
        break;
    default:
        for (int i = 0; i <= s; i++) gfx_rect(cx - s / 2 + i, cy - s + i, 1, (s - i) * 2 + 1, col);
        gfx_rect(cx + s / 2 + 1, cy - s, 2, s * 2 + 1, col);
        break;
    }
}

// Shuffle: two crossing lines with heads; drawn bright when on.
static void shuffle_icon(int cx, int cy, uint32_t col) {
    for (int i = -6; i <= 6; i++) {
        gfx_rect(cx + i, cy - i / 2 - 1, 1, 2, col);
        gfx_rect(cx + i, cy + i / 2 - 1, 1, 2, col);
    }
    gfx_rect(cx + 5, cy - 5, 3, 3, col);
    gfx_rect(cx + 5, cy + 3, 3, 3, col);
}

void ui_nowplaying_draw(App *a) {
    int cur = queue_current(&a->queue);
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    if (cur < 0) {
        const char *t = tr(S_MUSIC_NONE);
        gfx_text(FONT_TITLE, (SCREEN_W - gfx_text_width(FONT_TITLE, t)) / 2, 120, COL_TEXT, t);
        return;
    }
    const Track *t = &a->mlib.tracks[cur];
    int album = ui_music_album_of(&a->mlib, cur);
    // Background: soft glow in the album colour behind the cover.
    gfx_gradient_h(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x3a1f5aff, 0x0e131bff);
    gfx_gradient_v(0, CONTENT_Y + CONTENT_H - 60, SCREEN_W, 60, 0x0e131b00, 0x0e131bd0);

    int cs = 140, cx = 18, cy = CONTENT_Y + (CONTENT_H - cs) / 2;
    gfx_rect(cx + 4, cy + 6, cs, cs, 0x00000060);   // shadow
    ui_music_cover(a, album, cx, cy, cs);

    int x = cx + cs + 18, w = SCREEN_W - x - 14, y = CONTENT_Y + 18;
    char head[96], nofm[48];
    snprintf(nofm, sizeof nofm, tr(S_MUSIC_N_OF_M), a->queue.pos + 1, a->queue.count);
    snprintf(head, sizeof head, "%s · %s", tr(S_MUSIC_NOW_PLAYING), nofm);
    gfx_text_fit(FONT_SMALL, x, y, w, COL_MUTED, head);
    gfx_text_fit(FONT_TITLE, x, y + 14, w, COL_TEXT, t->title);
    char sub[240];
    snprintf(sub, sizeof sub, "%s — %s", artist_of(t), t->album);
    gfx_text_fit(FONT_SMALL, x, y + 40, w, 0xc9b8d8ff, sub);

    uint32_t pos = player_active() ? player_position_ms() : a->music_resume_ms, dur = player_duration_ms();
    int by = y + 62;
    NowFocus focus = a->mnav.np_focus;
    int np_top = mnav_top(&a->mnav) == MS_NOW;
    if (np_top && focus == NP_PROGRESS) gfx_round_rect(x - 4, by - 4, w + 8, 12, 6, 0x6fb0ff40);
    gfx_round_rect(x, by, w, 4, 2, 0x2b3646ff);
    if (dur > 0) gfx_round_rect(x, by, (int)((uint64_t)w * (pos > dur ? dur : pos) / dur), 4, 2, COL_ACCENT);
    char tp[16], td[16];
    fmt_time(pos, tp, sizeof tp);
    fmt_time(dur, td, sizeof td);
    gfx_text(FONT_SMALL, x, by + 7, COL_MUTED, tp);
    if (dur > 0) gfx_text(FONT_SMALL, x + w - gfx_text_width(FONT_SMALL, td), by + 7, COL_MUTED, td);

    // Controls: shuffle, prev, play/pause, next, repeat; the focused one gets a ring.
    int ry = by + 34, step = w / 5;
    uint32_t on = COL_ACCENT, off = COL_MUTED;
    if (np_top && focus >= NP_SHUFFLE) {
        int fx = x + step * (2 * (focus - NP_SHUFFLE) + 1) / 2;
        gfx_round_rect(fx - 16, ry - 16, 32, 32, 16, 0x6fb0ff40);
        gfx_round_rect_outline(fx - 16, ry - 16, 32, 32, 16, COL_ACCENT);
    }
    shuffle_icon(x + step / 2, ry, a->queue.shuffle ? on : off);
    transport(0, x + step * 3 / 2, ry, 5, COL_TEXT);
    gfx_round_rect(x + step * 5 / 2 - 13, ry - 13, 26, 26, 13, 0xffffffff);
    transport(music_playing(a) ? 2 : 1, x + step * 5 / 2 + (music_playing(a) ? 0 : 1), ry, 5, 0x111111ff);
    transport(3, x + step * 7 / 2, ry, 5, COL_TEXT);
    const char *rep = a->queue.repeat == REPEAT_ONE ? "↻1" : "↻";
    gfx_text(FONT_BOLD, x + step * 9 / 2 - 6, ry - 9, a->queue.repeat != REPEAT_OFF ? on : off, rep);

    // Up next: two tracks.
    int ny = ry + 22;
    gfx_text(FONT_SMALL, x, ny, COL_MUTED, tr(S_MUSIC_NEXT));
    for (int i = 1; i <= 2; i++) {
        int qi = a->queue.pos + i;
        if (qi >= a->queue.count) break;
        const Track *nt = &a->mlib.tracks[a->queue.items[qi]];
        gfx_text_fit(FONT_SMALL, x, ny + 4 + i * 13, w, i == 1 ? COL_TEXT : COL_MUTED, nt->title);
    }
    // Sleep timer countdown.
    if (a->sleep.choice != SLEEP_OFF) {
        char sl[64];
        if (a->sleep.choice == SLEEP_END_OF_LIST) {
            snprintf(sl, sizeof sl, "%s: %s", tr(S_MUSIC_SLEEP), tr(S_SLEEP_END_OF_LIST));
        } else {
            uint32_t left = sleep_remaining_ms(&a->sleep, a->now_ms) / 1000;
            snprintf(sl, sizeof sl, tr(S_SLEEP_LEFT), (int)(left / 60), (int)(left % 60));
        }
        gfx_text(FONT_SMALL, cx, cy + cs + 6, 0xffcf9fff, sl);
    }
}

// ---------- overlays ----------

static int panel(const char *title, int rows, int *x_out) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
    int shown = rows > 8 ? 8 : rows;
    int h = 34 + shown * ROW_H + 8;
    int x = SCREEN_W - PANEL_W - 12, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, PANEL_W, h, 10, 0x1a2230f8);
    gfx_round_rect_outline(x, y, PANEL_W, h, 10, 0xffffff20);
    gfx_text_fit(FONT_BOLD, x + 14, y + 9, PANEL_W - 28, COL_TEXT, title);
    *x_out = x;
    return y + 34;
}

static void row_bg(int x, int y, int selected) {
    if (selected) gfx_round_rect(x + 6, y, PANEL_W - 12, ROW_H - 2, 6, 0x2b5fa8ff);
}

static int first_visible(int sel, int rows) { return sel > 7 && rows > 8 ? (sel - 7 < rows - 8 ? sel - 7 : rows - 8) : 0; }

static void draw_menu(const App *a) {
    const char *title = a->mmenu_track >= 0 ? a->mlib.tracks[a->mmenu_track].title : music_side_name(a, a->mmenu_side);
    int x, y = panel(title, a->mmenu_count, &x);
    int sel = mnav_sel(&a->mnav);
    for (int i = 0; i < a->mmenu_count; i++, y += ROW_H) {
        row_bg(x, y, i == sel);
        char label[128];
        const char *track_id = a->mmenu_track >= 0 ? a->mlib.tracks[a->mmenu_track].id : "";
        switch (a->mmenu[i]) {
        case MM_TO_QUEUE: snprintf(label, sizeof label, "%s", tr(S_MUSIC_TO_QUEUE)); break;
        case MM_TO_PLAYLIST: snprintf(label, sizeof label, "%s", tr(S_MUSIC_TO_PLAYLIST)); break;
        case MM_FAVORITE:
            snprintf(label, sizeof label, "%s %s", mplaylist_has(&a->mdata->favorites, track_id) ? "★" : "☆",
                     tr(mplaylist_has(&a->mdata->favorites, track_id) ? S_MUSIC_FAV_REMOVE : S_MUSIC_FAV_ADD));
            break;
        case MM_QUEUE: snprintf(label, sizeof label, tr(S_MUSIC_QUEUE_N), a->queue.count - a->queue.pos); break;
        case MM_SLEEP: {
            char v[48];
            music_sleep_label(a, v, sizeof v);
            snprintf(label, sizeof label, "%s  ‹ %s ›", tr(S_MUSIC_SLEEP), v);
            break;
        }
        case MM_LISTEN: snprintf(label, sizeof label, "%s", tr(S_MUSIC_LISTEN)); break;
        case MM_RENAME_PLAYLIST: snprintf(label, sizeof label, "%s", tr(S_MUSIC_RENAME_PLAYLIST)); break;
        case MM_DELETE_PLAYLIST: snprintf(label, sizeof label, "%s", tr(S_MUSIC_DELETE_PLAYLIST)); break;
        }
        gfx_text_fit(FONT_BODY, x + 16, y + 3, PANEL_W - 30, a->mmenu[i] == MM_DELETE_PLAYLIST ? 0xff8a8aff : COL_TEXT, label);
    }
}

static void draw_queue(const App *a) {
    int rows = a->queue.count - a->queue.pos;
    int x, y = panel(tr(S_MUSIC_QUEUE), rows, &x);
    int sel = mnav_sel(&a->mnav), first = first_visible(sel, rows);
    for (int i = first; i < rows && i < first + 8; i++, y += ROW_H) {
        const Track *t = &a->mlib.tracks[a->queue.items[a->queue.pos + i]];
        row_bg(x, y, i == sel);
        if (i == 0) gfx_button_glyph(GLYPH_NOTE, x + 20, y + 10, 12);
        // Title, then the artist in a muted colour on the same line.
        int avail = PANEL_W - 46, tw = gfx_text_width(FONT_BODY, t->title);
        int title_w = tw < avail * 3 / 5 ? tw : avail * 3 / 5;
        gfx_text_fit(FONT_BODY, x + 32, y + 3, title_w, i == 0 && i != sel ? COL_ACCENT : COL_TEXT, t->title);
        gfx_text_fit(FONT_SMALL, x + 32 + title_w + 8, y + 5, avail - title_w - 8, i == sel ? 0xd0dcffff : COL_MUTED,
                     artist_of(t));
    }
}

static void draw_pick(const App *a) {
    int rows = a->mdata->list_count + 1;
    int x, y = panel(tr(S_MUSIC_TO_PLAYLIST), rows, &x);
    int sel = mnav_sel(&a->mnav), first = first_visible(sel, rows);
    const char *id = a->mpick_track >= 0 ? a->mlib.tracks[a->mpick_track].id : "";
    for (int i = first; i < rows && i < first + 8; i++, y += ROW_H) {
        row_bg(x, y, i == sel);
        if (i < a->mdata->list_count) {
            int in = mplaylist_has(&a->mdata->lists[i], id);
            gfx_round_rect_outline(x + 16, y + 5, 12, 12, 3, in ? COL_ACCENT : COL_MUTED);
            if (in) gfx_round_rect(x + 19, y + 8, 6, 6, 2, COL_ACCENT);
            gfx_text_fit(FONT_BODY, x + 36, y + 3, PANEL_W - 52, COL_TEXT, a->mdata->lists[i].name);
        } else {
            gfx_text(FONT_BODY, x + 16, y + 3, COL_ACCENT, tr(S_MUSIC_NEW_PLAYLIST));
        }
    }
}

static void draw_confirm(const App *a) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    int w = 320, h = 100, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    char msg[220];
    snprintf(msg, sizeof msg, tr(S_MUSIC_CONFIRM_DELETE), music_side_name(a, a->mmenu_side));
    char *dot = strstr(msg, "? ");
    if (dot) {
        dot[1] = '\0';
        gfx_text_fit(FONT_BODY, x + 16, y + 14, w - 32, COL_TEXT, msg);
        gfx_text_fit(FONT_SMALL, x + 16, y + 34, w - 32, COL_MUTED, dot + 2);
    } else {
        gfx_text_fit(FONT_BODY, x + 16, y + 20, w - 32, COL_TEXT, msg);
    }
    int sel = mnav_sel(&a->mnav);
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, i == sel ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *lt = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, lt)) / 2, by + 4, COL_TEXT, lt);
    }
}

static void draw_listen(const App *a) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    const char *t = tr(S_LISTEN_TITLE);
    gfx_text(FONT_BOLD, (SCREEN_W - gfx_text_width(FONT_BOLD, t)) / 2, 90, 0x6a7686ff, t);
    int y = 124;
    gfx_button_glyph(GLYPH_NOTE, 120, y + 7, 14);
    gfx_text(FONT_SMALL, 134, y, COL_MUTED, tr(S_LISTEN_PAUSE));
    gfx_button_glyph(GLYPH_NOTE, 120, y + 25, 14);
    gfx_text(FONT_SMALL, 134, y + 18, COL_MUTED, tr(S_LISTEN_TRACKS));
    gfx_text_fit(FONT_SMALL, 60, y + 40, SCREEN_W - 120, 0x4a5566ff, tr(S_LISTEN_WAKE));
    (void)a;
}

void ui_music_overlays_draw(App *a) {
    switch (mnav_top(&a->mnav)) {
    case MS_MENU: draw_menu(a); break;
    case MS_QUEUE: draw_queue(a); break;
    case MS_PICK: draw_pick(a); break;
    case MS_CONFIRM: draw_confirm(a); break;
    case MS_OSK: ui_osk_draw(a); break;
    case MS_LISTEN: draw_listen(a); break;
    default: break;
    }
}

void music_draw_hints(const App *a) {
    if (a->mlib.track_count == 0) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SCAN } };
        ui_hints_draw(a, a->music_scan_state == 1 ? NULL : h, a->music_scan_state == 1 ? 0 : 1);
        return;
    }
    switch (mnav_top(&a->mnav)) {
    case MS_NOW: {
        // The cursor makes per-button hints unnecessary; on the progress bar ←/→ seek.
        StrId first = a->mnav.np_focus == NP_PROGRESS ? S_HINT_SEEK : S_HINT_SELECT;
        Hint h[] = { { a->mnav.np_focus == NP_PROGRESS ? ACT_LEFT : ACT_CONFIRM, first },
                     { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_BACK }, { ACT_SELECT, S_HINT_SCREEN_OFF } };
        ui_hints_draw(a, h, 4);
        break;
    }
    case MS_QUEUE: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_JUMP }, { ACT_QUICK, S_HINT_REMOVE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 3);
        break;
    }
    case MS_MENU: case MS_PICK: case MS_CONFIRM: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
        break;
    }
    case MS_OSK: {
        static const Hint h[] = { { ACT_CONFIRM, S_OSK_HINT_TYPE }, { ACT_QUICK, S_OSK_HINT_DEL },
                                  { ACT_TAB_PREV, S_OSK_HINT_CAPS }, { ACT_BACK, S_OSK_HINT_CANCEL },
                                  { ACT_START, S_OSK_HINT_DONE } };
        ui_hints_draw(a, h, 5);
        break;
    }
    case MS_LISTEN: break;
    default: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_PLAY }, { ACT_MENU, S_HINT_MENU },
                                  { ACT_START, S_HINT_NOW_PLAYING } };
        ui_hints_draw(a, h, 3);
        break;
    }
    }
}
