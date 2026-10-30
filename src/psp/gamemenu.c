#include "ui.h"
#include "gfx.h"
#include "arkopts.h"
#include "dlcinst.h"
#include "dlcman.h"
#include "gamesize.h"
#include "gamefiles.h"
#include "log.h"
#include "platform.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Game card › △: saved data, install DLC / installed DLC (1.2.5), a clock for this game only, delete the game.
// "Add-ons (DLC)" opens a small menu of its own: install / installed (author: fewer items in the card menu).
enum { GM_MENU, GM_CLOCK, GM_DELETE_Q, GM_SAVES_Q, GM_DLC_MENU, GM_DLC_Q, GM_DLC_LIST, GM_DLC_DEL_Q };
enum { GI_SAVES, GI_DLC_MENU, GI_CLOCK, GI_DELETE, GI_COUNT };
enum { DI_INSTALL, DI_INSTALLED, DI_COUNT };

#define DLC_ROOT "ms0:/PSP/NEMO/install_dlc"
#define DLC_LICENSES "ms0:/PSP/LICENSE"
#define DLC_MAX 16

static DlcPack dlc_packs[DLC_MAX];
static int dlc_count, dlc_usable;
static char dlc_question[256];
static DlcEntry dlc_list[DLC_MAX];
static int dlc_list_count, dlc_marks[DLC_MAX], dlc_manual;   // manual: DLC there without Nemo's record

static void game_dir_of(const Game *g, char *out, size_t cap) { snprintf(out, cap, "ms0:/PSP/GAME/%s", g->disc_id); }
static void manifest_of(const Game *g, char *out, size_t cap) { snprintf(out, cap, "ms0:/PSP/NEMO/dlc/%s.txt", g->disc_id); }

// Installed DLC of the game: Nemo's records, else "installed by hand" when a DLC PARAM.PBP is there.
static void load_dlc_list(const Game *g) {
    char dir[64], man[64];
    game_dir_of(g, dir, sizeof dir);
    manifest_of(g, man, sizeof man);
    dlc_list_count = g->disc_id[0] ? dlcman_load(man, dir, dlc_list, DLC_MAX) : 0;
    dlc_manual = dlc_list_count == 0 && g->disc_id[0] && dlc_installed(dir);
    memset(dlc_marks, 0, sizeof dlc_marks);
}



static const Game *card_game(const App *a) {
    int i = a->nav.card_game;
    return i >= 0 && i < a->games.count ? &a->games.items[i] : NULL;
}

void gamemenu_open(App *a) {
    a->gm_open = 1;
    a->gm_sel = 0;
    a->gm_mode = GM_MENU;
    a->gm_quick = 0;
}

static int ask_delete(App *a, const Game *g) {
    GameTarget t[GAME_MAX_DISCS];
    if (game_delete_targets(g, t, GAME_MAX_DISCS) < 0) { ui_toast(a, tr(S_GM_DEL_REFUSED)); return 0; }
    a->gm_mode = GM_DELETE_Q;
    a->gm_yes = 0;                            // "No" first: deleting is not undoable
    return 1;
}

void gamemenu_ask_delete(App *a) {
    const Game *g = card_game(a);
    a->gm_open = 0;
    if (!g || !ask_delete(a, g)) return;
    a->gm_open = 1;
    a->gm_quick = 1;
}

static int clock_index(const App *a, const Game *g) {
    int mhz = a->ark_text ? arkclock_game_get(a->ark_text, g->disc_id) : 0;
    for (int i = 0; i < 8; i++) if (ARK_CLOCKS[i] == mhz) return i;
    return 0;
}

static void set_clock(App *a, const Game *g, int index) {
    if (!a->ark_text) { ui_fail(a, S_ST_ARK_READ_FAILED); return; }
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
        ui_fail(a, S_ST_REG_FAILED);
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

// "Install DLC": find the packs for this game, then ask (adapting another version's DLC is asked explicitly).
static void dlc_start(App *a, const Game *g) {
    dlc_count = g->disc_id[0] ? dlc_find(DLC_ROOT, DLC_LICENSES, g->disc_id, g->title, dlc_packs, DLC_MAX) : 0;
    dlc_usable = 0;
    const char *other = NULL;
    for (int i = 0; i < dlc_count; i++) {
        log_msg("dlc: found %s (%s, %s%s)", dlc_packs[i].dir, dlc_packs[i].disc_id,
                dlc_packs[i].match == DLC_SAME ? "same" : "other", dlc_packs[i].encrypted ? ", encrypted" : "");
        if (dlc_packs[i].encrypted) continue;
        dlc_usable++;
        if (dlc_packs[i].match == DLC_OTHER && !other) other = dlc_packs[i].disc_id;
    }
    if (dlc_count == 0) { ui_toast(a, tr(S_DLC_NONE)); return; }   // stays in "Add-ons (DLC)"
    if (dlc_usable == 0) { ui_toast(a, tr(S_DLC_ENCRYPTED)); return; }
    if (other) snprintf(dlc_question, sizeof dlc_question, tr(S_DLC_ADAPT_FMT), other, g->disc_id);
    else snprintf(dlc_question, sizeof dlc_question, tr(S_DLC_ASK_FMT), dlc_usable);
    a->gm_mode = GM_DLC_Q;
    a->gm_yes = 0;
}

static void dlc_progress(void *ctx, int percent, const char *file) {
    (void)ctx;
    char line[64];
    snprintf(line, sizeof line, tr(S_DLC_PROGRESS_FMT), percent);
    int w = 360, h = 70, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_frame_begin();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_text_fit(FONT_BODY, x + 16, y + 12, w - 32, COL_TEXT, line);
    gfx_text_fit(FONT_SMALL, x + 16, y + 34, w - 32, COL_MUTED, file);
    gfx_round_rect(x + 16, y + 54, w - 32, 6, 3, COL_PANEL);
    if (percent > 0) gfx_round_rect(x + 16, y + 54, (w - 32) * (percent > 100 ? 100 : percent) / 100, 6, 3, COL_ACCENT);
    gfx_frame_end();
}

static void dlc_run(App *a, const Game *g) {
    char game_dir[64], man[64];
    game_dir_of(g, game_dir, sizeof game_dir);
    manifest_of(g, man, sizeof man);
    sceIoMkdir("ms0:/PSP/NEMO/dlc", 0777);
    uint32_t t0 = platform_ms();
    int n = dlc_install(dlc_packs, dlc_count, DLC_ROOT, game_dir, g->disc_id, man, g->title, dlc_progress, NULL);
    log_msg("dlc: install into %s: %d of %d packs", game_dir, n, dlc_usable);
    log_time("dlc_install", platform_ms() - t0);
    a->gm_open = 0;
    a->card_dlc = dlc_installed(game_dir);
    char msg[96];
    int skipped = dlc_count - dlc_usable;   // encrypted ones stay in install_dlc
    if (n > 0 && skipped > 0) snprintf(msg, sizeof msg, tr(S_DLC_DONE_SKIP_FMT), n, skipped);
    else if (n > 0) snprintf(msg, sizeof msg, tr(S_DLC_DONE_FMT), n);
    if (n > 0) ui_toast(a, msg);
    else ui_fail(a, S_DLC_FAIL);
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
        if (r == 0 && a->gm_quick) {          // asked from the shelves: back to them, not to the card
            a->gm_open = 0;
            nav_pop(&a->nav);
        } else if (r == 0) a->gm_mode = GM_MENU;
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
    case GM_DLC_MENU:
        if (act == ACT_UP && a->gm_pick > 0) a->gm_pick--;
        else if (act == ACT_DOWN && a->gm_pick < DI_COUNT - 1) a->gm_pick++;
        else if (act == ACT_BACK) { a->gm_mode = GM_MENU; }
        else if (act == ACT_CONFIRM && a->gm_pick == DI_INSTALL) dlc_start(a, g);
        else if (act == ACT_CONFIRM) {
            load_dlc_list(g);
            if (dlc_list_count == 0 && !dlc_manual) { ui_toast(a, tr(S_DLC_NONE_INSTALLED)); return; }
            a->gm_list = 0;
            a->gm_mode = GM_DLC_LIST;
        }
        return;
    case GM_DLC_Q: {
        int r = yes_no(a, act);
        if (r == 0) a->gm_mode = GM_DLC_MENU;
        else if (r == 1) dlc_run(a, g);
        return;
    }
    case GM_DLC_LIST: {
        int rows = dlc_manual ? 1 : dlc_list_count, marked = 0;
        for (int i = 0; i < dlc_list_count; i++) marked += dlc_marks[i];
        if (act == ACT_UP && a->gm_list > 0) a->gm_list--;
        else if (act == ACT_DOWN && a->gm_list < rows - 1) a->gm_list++;
        else if (act == ACT_BACK) a->gm_mode = GM_DLC_MENU;
        else if (act == ACT_CONFIRM && !dlc_manual && a->gm_list < dlc_list_count) dlc_marks[a->gm_list] ^= 1;
        else if (act == ACT_MENU && !dlc_manual) {
            if (!marked) ui_toast(a, tr(S_DLC_EMPTY_MARK));
            else {
                snprintf(dlc_question, sizeof dlc_question, tr(S_DLC_DEL_Q_FMT), marked);
                a->gm_mode = GM_DLC_DEL_Q;
                a->gm_yes = 0;
            }
        }
        return;
    }
    case GM_DLC_DEL_Q: {
        int r = yes_no(a, act);
        if (r == 0) a->gm_mode = GM_DLC_LIST;
        else if (r == 1) {
            char dir[64], man[64], msg[64];
            game_dir_of(g, dir, sizeof dir);
            manifest_of(g, man, sizeof man);
            int n = dlcman_delete(man, dir, dlc_marks, dlc_list_count);
            log_msg("dlc: deleted %d from %s", n, dir);
            snprintf(msg, sizeof msg, tr(S_DLC_DELETED_FMT), n);
            ui_toast(a, msg);
            a->card_dlc = dlc_installed(dir);
            a->gm_mode = GM_DLC_MENU;
        }
        return;
    }
    default: break;
    }
    if (act == ACT_UP && a->gm_sel > 0) a->gm_sel--;
    else if (act == ACT_DOWN && a->gm_sel < GI_COUNT - 1) a->gm_sel++;
    else if (act == ACT_BACK || act == ACT_MENU) a->gm_open = 0;
    else if (act == ACT_CONFIRM) {
        int item = a->gm_sel;
        if (item == GI_DLC_MENU) {   // no game ID: "no matching DLC" inside (author)
            a->gm_pick = 0;
            a->gm_mode = GM_DLC_MENU;
            return;
        }
        if (item != GI_DELETE && !g->disc_id[0]) { ui_toast(a, tr(S_GM_NO_ID)); return; }
        if (item == GI_SAVES) {
            char t[160];
            snprintf(t, sizeof t, tr(S_SV_TITLE_FMT), g->title);
            a->gm_open = 0;
            saves_open(a, g->disc_id, t);
        } else if (item == GI_CLOCK) {
            if (!a->ark_text) a->ark_text = platform_ark_read_text();
            a->gm_pick = clock_index(a, g);
            a->gm_mode = GM_CLOCK;
        } else {
            ask_delete(a, g);
        }
    }
}

// Up to `lines` lines, broken at spaces (the last one is cut to fit).
static void text_wrap(FontId f, int x, int y, int w, int lines, uint32_t col, const char *msg) {
    const char *p = msg;
    for (int l = 0; l < lines && *p; l++, y += gfx_text_height(f) + 2) {
        char line[256];
        int n = 0, cut = 0;
        while (p[n] && n < (int)sizeof line - 1) {
            line[n] = p[n];
            line[n + 1] = 0;
            if (gfx_text_width(f, line) > w) break;
            if (p[n] == ' ') cut = n;
            n++;
        }
        if (!p[n] || l == lines - 1) cut = n;
        else if (cut == 0) cut = n;
        line[cut] = 0;
        gfx_text_fit(f, x, y, w, col, line);
        p += cut;
        while (*p == ' ') p++;
    }
}

static void question(const App *a, const char *msg, int danger) {
    int w = 380, h = 104, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, danger ? 0xd9534fff : COL_ACCENT);
    text_wrap(FONT_BODY, x + 16, y + 10, w - 32, 3, COL_TEXT, msg);
    const StrId B[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36, on = (i == 0) == a->gm_yes;
        gfx_round_rect(bx, by, 104, 24, 12, on ? (i == 0 && danger ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *t = tr(B[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}

void gamemenu_draw(const App *a) {
    const Game *g = card_game(a);
    if (!g) return;
    if (a->gm_mode == GM_DELETE_Q) {
        char msg[200];
        snprintf(msg, sizeof msg, tr(S_GM_DEL_FOREVER_FMT), g->title);
        question(a, msg, 1);
        return;
    }
    if (a->gm_mode == GM_SAVES_Q) { question(a, tr(S_GM_DEL_SAVES_Q), 1); return; }
    if (a->gm_mode == GM_DLC_Q) { question(a, dlc_question, 0); return; }
    if (a->gm_mode == GM_DLC_DEL_Q) { question(a, dlc_question, 1); return; }
    if (a->gm_mode == GM_DLC_LIST) {   // installed DLC: marks, names, sizes
        int rows = dlc_manual ? 1 : dlc_list_count;
        int w = 340, rh = 24, h = 34 + rows * rh + 8, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
        gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
        gfx_round_rect(x, y, w, h, 10, 0x1a2230f8);
        gfx_round_rect_outline(x, y, w, h, 10, 0xffffff20);
        gfx_text_fit(FONT_BOLD, x + 14, y + 9, w - 28, COL_TEXT, tr(S_GM_DLC_LIST));
        for (int i = 0; i < rows; i++) {
            int ry = y + 34 + i * rh;
            if (i == a->gm_list) gfx_round_rect(x + 6, ry, w - 12, rh - 2, 6, 0x2b5fa8ff);
            if (dlc_manual) { gfx_text_fit(FONT_BODY, x + 16, ry + 3, w - 32, COL_MUTED, tr(S_DLC_MANUAL)); break; }
            gfx_round_rect_outline(x + 14, ry + 5, 12, 12, 3, COL_TEXT);
            if (dlc_marks[i]) gfx_round_rect(x + 17, ry + 8, 6, 6, 2, COL_ACCENT);
            char size[24];
            game_size_text(size, sizeof size, dlc_list[i].bytes, i18n_get() == LANG_EN);
            int sw = gfx_text_width(FONT_SMALL, size);
            gfx_text_fit(FONT_BODY, x + 34, ry + 3, w - 60 - sw, COL_TEXT, dlc_list[i].name);
            gfx_text(FONT_SMALL, x + w - 14 - sw, ry + 5, COL_MUTED, size);
        }
        return;
    }
    int clock = a->gm_mode == GM_CLOCK, dlcm = a->gm_mode == GM_DLC_MENU;
    int count = clock ? 8 : dlcm ? DI_COUNT : GI_COUNT, sel = clock || dlcm ? a->gm_pick : a->gm_sel;
    int w = 230, rh = 24, h = 34 + count * rh + 8, x = SCREEN_W - w - 16, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
    gfx_round_rect(x, y, w, h, 10, 0x1a2230f8);
    gfx_round_rect_outline(x, y, w, h, 10, 0xffffff20);
    gfx_text_fit(FONT_BOLD, x + 14, y + 9, w - 28, COL_TEXT, clock ? tr(S_GM_CLOCK) : dlcm ? tr(S_GM_DLC_MENU) : g->title);
    static const StrId ITEMS[GI_COUNT] = { S_GM_SAVES, S_GM_DLC_MENU, S_GM_CLOCK, S_GM_DELETE };
    static const StrId DLC_ITEMS[DI_COUNT] = { S_GM_DLC, S_GM_DLC_LIST };
    int cur = clock ? clock_index(a, g) : -1;
    for (int i = 0; i < count; i++) {
        int ry = y + 34 + i * rh;
        if (i == sel) gfx_round_rect(x + 6, ry, w - 12, rh - 2, 6, 0x2b5fa8ff);
        char s[64];
        if (dlcm) snprintf(s, sizeof s, "%s", tr(DLC_ITEMS[i]));
        else if (!clock) snprintf(s, sizeof s, "%s", tr(ITEMS[i]));
        else if (!ARK_CLOCKS[i]) snprintf(s, sizeof s, "%s", tr(S_GM_CLOCK_AUTO));
        else snprintf(s, sizeof s, tr(ARK_CLOCKS[i] > 333 ? S_ST_MHZ_EXP_FMT : S_ST_MHZ_FMT), ARK_CLOCKS[i]);
        uint32_t col = i == cur ? COL_ACCENT : (!clock && !dlcm && i == GI_DELETE) ? 0xff8a8aff : COL_TEXT;
        gfx_text_fit(FONT_BODY, x + 16, ry + 3, w - 32, col, s);
    }
}

void gamemenu_draw_hints(const App *a) {
    if (a->gm_mode == GM_DLC_LIST && !dlc_manual) {
        static const Hint l[] = { { ACT_CONFIRM, S_HINT_MARK }, { ACT_MENU, S_HINT_DELETE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, l, 3);
        return;
    }
    static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
    ui_hints_draw(a, h, 2);
}
