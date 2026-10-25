#include "ui.h"
#include "gfx.h"
#include "arkopts.h"
#include "gamefiles.h"
#include "log.h"
#include "platform.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Game card › △: saved data, a clock for this game only, delete the game (1.1.10).
enum { GM_MENU, GM_CLOCK, GM_DELETE_Q, GM_SAVES_Q };
enum { GI_SAVES, GI_CLOCK, GI_DELETE, GI_COUNT };

static const Game *card_game(const App *a) {
    int i = a->nav.card_game;
    return i >= 0 && i < a->games.count ? &a->games.items[i] : NULL;
}

void gamemenu_open(App *a) {
    a->gm_open = 1;
    a->gm_sel = 0;
    a->gm_mode = GM_MENU;
}

static int clock_index(const App *a, const Game *g) {
    int mhz = a->ark_text ? arkclock_game_get(a->ark_text, g->disc_id) : 0;
    for (int i = 0; i < 8; i++) if (ARK_CLOCKS[i] == mhz) return i;
    return 0;
}

static void set_clock(App *a, const Game *g, int index) {
    if (!a->ark_text) { ui_toast(a, tr(S_ST_ARK_READ_FAILED)); return; }
    char *next = arkclock_game_set(a->ark_text, g->disc_id, ARK_CLOCKS[index]);
    if (!next) return;
    int rc = platform_ark_write_text(next);
    log_msg("game: %s clock %d rc=%d", g->disc_id, ARK_CLOCKS[index], rc);
    if (rc == 0) {
        free(a->ark_text);
        a->ark_text = next;
        ui_toast(a, tr(S_ST_APPLY_RESTART));
    } else {
        free(next);
        ui_toast(a, tr(S_ST_REG_FAILED));
    }
}

static void delete_game(App *a, const Game *g, int with_saves) {
    GameTarget t[GAME_MAX_DISCS];
    int n = game_delete_targets(g, t, GAME_MAX_DISCS), failed = 0;
    char code[16];
    snprintf(code, sizeof code, "%s", g->disc_id);
    log_msg("game: delete \"%s\" (%d targets, saves %d)", g->title, n, with_saves);
    for (int i = 0; i < n; i++) {
        int rc = t[i].is_dir ? platform_rmtree(t[i].path) : (sceIoRemove(t[i].path) < 0 ? -1 : 0);
        log_msg("game: delete %s rc=%d", t[i].path, rc);
        if (rc != 0) failed = 1;
    }
    if (with_saves && code[0]) saves_delete_code(code);
    a->gm_open = 0;
    nav_pop(&a->nav);                         // the card closes before the list changes
    library_rescan(a);
    ui_toast(a, tr(failed ? S_GM_DEL_FAIL : S_GM_DELETED));
}

static int yes_no(App *a, Action act) {       // 1 yes, 0 no, -1 still asking
    if (act == ACT_LEFT || act == ACT_RIGHT) a->gm_yes = !a->gm_yes;
    else if (act == ACT_BACK) return 0;
    else if (act == ACT_CONFIRM) return a->gm_yes;
    return -1;
}

void gamemenu_action(App *a, Action act) {
    const Game *g = card_game(a);
    if (!g) { a->gm_open = 0; return; }
    switch (a->gm_mode) {
    case GM_CLOCK:
        if (act == ACT_UP && a->gm_pick > 0) a->gm_pick--;
        else if (act == ACT_DOWN && a->gm_pick < 7) a->gm_pick++;
        else if (act == ACT_BACK) a->gm_mode = GM_MENU;
        else if (act == ACT_CONFIRM) {
            if (a->gm_pick != clock_index(a, g)) set_clock(a, g, a->gm_pick);
            a->gm_open = 0;
        }
        return;
    case GM_DELETE_Q: {
        int r = yes_no(a, act);
        if (r == 0) a->gm_mode = GM_MENU;
        else if (r == 1) {
            if (g->disc_id[0]) { a->gm_mode = GM_SAVES_Q; a->gm_yes = 0; }
            else delete_game(a, g, 0);
        }
        return;
    }
    case GM_SAVES_Q: {
        int r = yes_no(a, act);
        if (r >= 0) delete_game(a, g, r);
        return;
    }
    default: break;
    }
    if (act == ACT_UP && a->gm_sel > 0) a->gm_sel--;
    else if (act == ACT_DOWN && a->gm_sel < GI_COUNT - 1) a->gm_sel++;
    else if (act == ACT_BACK || act == ACT_MENU) a->gm_open = 0;
    else if (act == ACT_CONFIRM) {
        if (a->gm_sel != GI_DELETE && !g->disc_id[0]) { ui_toast(a, tr(S_GM_NO_ID)); return; }
        if (a->gm_sel == GI_SAVES) {
            char t[160];
            snprintf(t, sizeof t, tr(S_SV_TITLE_FMT), g->title);
            a->gm_open = 0;
            saves_open(a, g->disc_id, t);
        } else if (a->gm_sel == GI_CLOCK) {
            if (!a->ark_text) a->ark_text = platform_ark_read_text();
            a->gm_pick = clock_index(a, g);
            a->gm_mode = GM_CLOCK;
        } else {
            GameTarget t[GAME_MAX_DISCS];
            if (game_delete_targets(g, t, GAME_MAX_DISCS) < 0) { ui_toast(a, tr(S_GM_DEL_REFUSED)); return; }
            a->gm_mode = GM_DELETE_Q;
            a->gm_yes = 0;                    // "No" first: deleting is not undoable
        }
    }
}

static void question(const App *a, const char *msg) {
    int w = 380, h = 104, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, 0xd9534fff);
    gfx_text_fit(FONT_BODY, x + 16, y + 16, w - 32, COL_TEXT, msg);
    const StrId B[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36, on = (i == 0) == a->gm_yes;
        gfx_round_rect(bx, by, 104, 24, 12, on ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *t = tr(B[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}

void gamemenu_draw(const App *a) {
    const Game *g = card_game(a);
    if (!g) return;
    if (a->gm_mode == GM_DELETE_Q) {
        char msg[200];
        snprintf(msg, sizeof msg, tr(S_GM_DEL_Q_FMT), g->title);
        question(a, msg);
        return;
    }
    if (a->gm_mode == GM_SAVES_Q) { question(a, tr(S_GM_DEL_SAVES_Q)); return; }
    int clock = a->gm_mode == GM_CLOCK, count = clock ? 8 : GI_COUNT, sel = clock ? a->gm_pick : a->gm_sel;
    int w = 230, rh = 24, h = 34 + count * rh + 8, x = SCREEN_W - w - 16, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
    gfx_round_rect(x, y, w, h, 10, 0x1a2230f8);
    gfx_round_rect_outline(x, y, w, h, 10, 0xffffff20);
    gfx_text_fit(FONT_BOLD, x + 14, y + 9, w - 28, COL_TEXT, clock ? tr(S_GM_CLOCK) : g->title);
    static const StrId ITEMS[GI_COUNT] = { S_GM_SAVES, S_GM_CLOCK, S_GM_DELETE };
    int cur = clock ? clock_index(a, g) : -1;
    for (int i = 0; i < count; i++) {
        int ry = y + 34 + i * rh;
        if (i == sel) gfx_round_rect(x + 6, ry, w - 12, rh - 2, 6, 0x2b5fa8ff);
        char s[64];
        if (!clock) snprintf(s, sizeof s, "%s", tr(ITEMS[i]));
        else if (!ARK_CLOCKS[i]) snprintf(s, sizeof s, "%s", tr(S_GM_CLOCK_AUTO));
        else snprintf(s, sizeof s, tr(ARK_CLOCKS[i] > 333 ? S_ST_MHZ_EXP_FMT : S_ST_MHZ_FMT), ARK_CLOCKS[i]);
        uint32_t col = i == cur ? COL_ACCENT : (!clock && i == GI_DELETE) ? 0xff8a8aff : COL_TEXT;
        gfx_text_fit(FONT_BODY, x + 16, ry + 3, w - 32, col, s);
    }
}

void gamemenu_draw_hints(const App *a) {
    static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
    ui_hints_draw(a, h, 2);
}
