#include "ui.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

#define PANEL_W 240
#define ROW_H 24

static const StrId MENU_LABELS[] = {
    [MI_TO_FOLDER] = S_MENU_ADD_TO_FOLDER,
    [MI_REMOVE_FROM_FOLDER] = S_MENU_REMOVE_FROM_FOLDER,
    [MI_DETAILS] = S_MENU_DETAILS,
    [MI_RENAME_FOLDER] = S_MENU_RENAME_FOLDER,
    [MI_DELETE_FOLDER] = S_MENU_DELETE_FOLDER,
};

static int top_sel(const App *app) {
    return app->nav.depth ? app->nav.stack[app->nav.depth - 1].sel : 0;
}

// Dimmed backdrop + panel on the right; returns the y of the first row.
static int panel(const char *title, int rows, int *x_out) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
    int h = 34 + rows * ROW_H + 8;
    if (h > CONTENT_H - 8) h = CONTENT_H - 8;
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

void ui_menu_draw(const App *app) {
    const char *title = app->menu_game >= 0 ? app->games.items[app->menu_game].title : "";
    if (app->menu_folder >= 0 && app->menu_game < 0) title = app->lib->folders[app->menu_folder].name;
    int x, y = panel(title, app->menu_count, &x);
    int sel = top_sel(app);
    for (int i = 0; i < app->menu_count; i++, y += ROW_H) {
        row_bg(x, y, i == sel);
        uint32_t col = app->menu[i] == MI_DELETE_FOLDER ? 0xff8a8aff : COL_TEXT;
        gfx_text(FONT_BODY, x + 16, y + 3, col, tr(MENU_LABELS[app->menu[i]]));
    }
}

// Rows: user folders (with a check mark when the game is inside) and "New folder…".
void ui_folder_pick_draw(const App *app) {
    const Game *g = &app->games.items[app->pick_game];
    int rows = app->lib->folder_count + 1;
    int x, y0 = panel(tr(S_FOLDER_PICK_TITLE), rows > 7 ? 7 : rows, &x);
    int sel = top_sel(app);
    int first = sel > 6 ? sel - 6 : 0;   // simple scrolling for long lists
    for (int i = first; i < rows && i < first + 7; i++) {
        int y = y0 + (i - first) * ROW_H;
        row_bg(x, y, i == sel);
        if (i < app->lib->folder_count) {
            int in = library_folder_has(app->lib, i, g->id);
            gfx_round_rect_outline(x + 16, y + 5, 12, 12, 3, in ? COL_ACCENT : COL_MUTED);
            if (in) gfx_round_rect(x + 19, y + 8, 6, 6, 2, COL_ACCENT);
            gfx_text_fit(FONT_BODY, x + 36, y + 3, PANEL_W - 52, COL_TEXT, app->lib->folders[i].name);
        } else {
            gfx_text(FONT_BODY, x + 16, y + 3, COL_ACCENT, tr(S_FOLDER_NEW));
        }
    }
}

void ui_confirm_draw(const App *app) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    int w = 320, h = 100, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    char msg[200];
    const char *name = app->menu_folder >= 0 ? app->lib->folders[app->menu_folder].name : "";
    snprintf(msg, sizeof msg, tr(S_CONFIRM_DELETE_FOLDER), name);
    // Two lines at most: split at the sentence break if it does not fit.
    char *dot = strstr(msg, "? ");
    if (gfx_text_width(FONT_BODY, msg) > w - 32 && dot) {
        dot[1] = '\0';
        gfx_text_fit(FONT_BODY, x + 16, y + 14, w - 32, COL_TEXT, msg);
        gfx_text_fit(FONT_SMALL, x + 16, y + 34, w - 32, COL_MUTED, dot + 2);
    } else {
        gfx_text_fit(FONT_BODY, x + 16, y + 20, w - 32, COL_TEXT, msg);
    }
    int sel = top_sel(app);
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, i == sel ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *t = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}

static const char *key_label(const Osk *o, int row, int col, char *buf, size_t cap) {
    const char *k = osk_key(o, row, col);
    if (strcmp(k, "SHIFT") == 0) return "";   // drawn as an arrow icon
    if (strcmp(k, "SPACE") == 0) return tr(S_OSK_SPACE);
    if (strcmp(k, "DEL") == 0) return tr(S_OSK_DEL);
    if (strcmp(k, "DONE") == 0) return tr(S_OSK_DONE);
    if (strcmp(k, "SYMBOLS") == 0) return "!#1";
    if (strcmp(k, "LETTERS") == 0) return "abc";
    if (strcmp(k, "LANG") == 0) {
        snprintf(buf, cap, "%s", o->layout ? "EN" : "RU");
        return buf;
    }
    return k;
}

void ui_osk_draw(const App *app) {
    const Osk *o = &app->osk;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    int top = CONTENT_Y + 6;
    gfx_round_rect(8, top, SCREEN_W - 16, CONTENT_H - 12, 10, 0x1a2230ff);
    StrId tid = app->osk_purpose == OSK_FOR_RENAME ? S_OSK_TITLE_RENAME
              : app->osk_purpose == OSK_FOR_NEW_FOLDER ? S_OSK_TITLE_NEW
              : app->osk_purpose == OSK_FOR_NICKNAME ? S_ST_NICK_TITLE : S_MUSIC_PLAYLIST_NAME;
    char title[96];
    if (app->osk_purpose == OSK_FOR_WIFI_PASSWORD) snprintf(title, sizeof title, tr(S_NET_PASSWORD_FOR), app->net_target.ssid);
    else snprintf(title, sizeof title, "%s", tr(tid));
    gfx_text_fit(FONT_SMALL, 20, top + 6, SCREEN_W - 120, COL_MUTED, title);
    if (o->max_len > 0) {
        char cnt[32];
        snprintf(cnt, sizeof cnt, "%d / %d", (int)strlen(o->text), o->max_len);
        gfx_text(FONT_SMALL, SCREEN_W - 22 - gfx_text_width(FONT_SMALL, cnt), top + 6,
                 osk_can_finish(o) ? COL_MUTED : COL_WARN, cnt);
    }

    // Input field with a blinking caret; a hidden password shows one bullet per character.
    char shown[OSK_TEXT_MAX * 3 + 1];
    if (osk_hidden(o)) {
        size_t n = strlen(o->text), k = 0;
        for (size_t i = 0; i < n && k + 3 < sizeof shown; i++) { memcpy(shown + k, "\xe2\x80\xa2", 3); k += 3; }
        shown[k] = '\0';
    } else {
        snprintf(shown, sizeof shown, "%s", o->text);
    }
    gfx_round_rect(18, top + 22, SCREEN_W - 36, 24, 6, 0x0e131bff);
    gfx_text_fit(FONT_BODY, 26, top + 25, SCREEN_W - 60, COL_TEXT, shown);
    int tw = gfx_text_width(FONT_BODY, shown);
    if (tw > SCREEN_W - 60) tw = SCREEN_W - 60;
    if ((app->now_ms / 500) % 2 == 0) gfx_rect(27 + tw, top + 27, 2, 15, COL_ACCENT);

    int key_y = top + 54, key_h = 26;
    for (int r = 0; r < osk_rows(); r++) {
        int cols = osk_cols(o, r);
        int special = r == osk_rows() - 1;
        int gap = 3, avail = SCREEN_W - 36;
        int key_w = (avail - gap * (cols - 1)) / cols;
        if (!special && key_w > 36) key_w = 36;
        int row_w = cols * key_w + (cols - 1) * gap;
        int x = (SCREEN_W - row_w) / 2;
        for (int c = 0; c < cols; c++, x += key_w + gap) {
            int sel = r == o->row && c == o->col;
            gfx_round_rect(x, key_y, key_w, key_h - 3, 5, sel ? 0x2b5fa8ff : 0x2a3442ff);
            char buf[8];
            const char *label = key_label(o, r, c, buf, sizeof buf);
            if (strcmp(osk_key(o, r, c), "SHIFT") == 0) {
                // Shift: up arrow, filled while capitals are on (SHIFT latched or L held).
                int on = o->shift || o->hold, cx = x + key_w / 2, cy = key_y + 10;
                uint32_t col = on ? COL_ACCENT : COL_TEXT;
                for (int i = 0; i < 6; i++) gfx_rect(cx - i, cy - 4 + i, i * 2 + 1, 1, col);
                if (on) gfx_rect(cx - 2, cy + 2, 5, 6, col);
                else gfx_round_rect_outline(cx - 2, cy + 2, 5, 6, 1, col);
                continue;
            }
            FontId f = special ? FONT_SMALL : FONT_BODY;
            int lw = gfx_text_width(f, label);
            gfx_text_fit(f, x + (key_w - (lw < key_w - 4 ? lw : key_w - 4)) / 2, key_y + (special ? 5 : 3),
                         key_w - 4, sel ? 0xffffffff : COL_TEXT, label);
        }
        key_y += key_h;
    }
}
