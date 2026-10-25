#include "ui.h"
#include "gfx.h"
#include "gamefiles.h"
#include "log.h"
#include "platform.h"
#include "sfo.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Saved data in ms0:/PSP/SAVEDATA: one game's (from its card) or every game's (Settings), with
// "no game" for saves whose game is not on the card any more. △ deletes one save.
#define SAVES_DIR "ms0:/PSP/SAVEDATA"
#define SAVES_MAX 200
#define ROW_H 26
#define VISIBLE 7

typedef struct {
    char dir[48], code[10], title[96], game[96];
    long size;
    int orphan;
} SaveEntry;

typedef struct SavesScreen {
    SaveEntry items[SAVES_MAX];
    int count, sel, top, confirm, confirm_yes, all;
    char heading[128];
} SavesScreen;

static void read_sfo(const char *dir, SaveEntry *e) {
    char path[160];
    snprintf(path, sizeof path, SAVES_DIR "/%s/PARAM.SFO", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    static uint8_t buf[8192];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (sfo_get_string(buf, n, "SAVEDATA_TITLE", e->title, sizeof e->title) != 0 || !e->title[0])
        sfo_get_string(buf, n, "SAVEDATA_DETAIL", e->title, sizeof e->title);
    sfo_get_string(buf, n, "TITLE", e->game, sizeof e->game);
}

static int has_game(const App *a, const char *code) {
    for (int i = 0; i < a->games.count; i++)
        if (strcmp(a->games.items[i].disc_id, code) == 0) return 1;
    return 0;
}

static int by_game(const void *x, const void *y) {
    const SaveEntry *a = x, *b = y;
    int c = strcmp(a->game, b->game);
    return c ? c : strcmp(a->dir, b->dir);
}

static void scan(App *a, SavesScreen *s, const char *code) {
    s->count = 0;
    SceUID d = sceIoDopen(SAVES_DIR);
    if (d < 0) return;
    SceIoDirent de;
    memset(&de, 0, sizeof de);
    while (sceIoDread(d, &de) > 0 && s->count < SAVES_MAX) {
        SaveEntry *e = &s->items[s->count];
        memset(e, 0, sizeof *e);
        if (FIO_S_ISDIR(de.d_stat.st_mode) && save_game_code(de.d_name, e->code) == 0 &&
            (!code || strcmp(code, e->code) == 0)) {
            snprintf(e->dir, sizeof e->dir, "%s", de.d_name);
            read_sfo(e->dir, e);
            if (!e->title[0]) memcpy(e->title, e->dir, sizeof e->dir);   // dir/code are shorter than the targets
            if (!e->game[0]) memcpy(e->game, e->code, sizeof e->code);
            char path[96];
            snprintf(path, sizeof path, SAVES_DIR "/%s", e->dir);
            e->size = platform_dir_size(path);
            e->orphan = !has_game(a, e->code);
            s->count++;
        }
        memset(&de, 0, sizeof de);
    }
    sceIoDclose(d);
    qsort(s->items, (size_t)s->count, sizeof s->items[0], by_game);
}

void saves_open(App *a, const char *game_code, const char *title) {
    SavesScreen *s = calloc(1, sizeof *s);
    if (!s) return;
    s->all = game_code == NULL;
    snprintf(s->heading, sizeof s->heading, "%s", title);
    scan(a, s, game_code);
    log_msg("saves: %d for %s", s->count, game_code ? game_code : "all games");
    a->saves = s;
}

int saves_delete_code(const char *code) {
    int n = 0;
    SceUID d = sceIoDopen(SAVES_DIR);
    if (d < 0) return 0;
    char dirs[64][48];
    int count = 0;
    SceIoDirent de;
    memset(&de, 0, sizeof de);
    char c[10];
    while (sceIoDread(d, &de) > 0 && count < 64) {
        if (FIO_S_ISDIR(de.d_stat.st_mode) && save_game_code(de.d_name, c) == 0 && strcmp(c, code) == 0)
            snprintf(dirs[count++], sizeof dirs[0], "%s", de.d_name);
        memset(&de, 0, sizeof de);
    }
    sceIoDclose(d);
    for (int i = 0; i < count; i++) {           // after closing the listing: deleting while reading skips entries
        char path[96];
        snprintf(path, sizeof path, SAVES_DIR "/%s", dirs[i]);
        if (platform_rmtree(path) == 0) n++;
    }
    log_msg("saves: deleted %d of %d for %s", n, count, code);
    return n;
}

void saves_action(App *a, Action act) {
    SavesScreen *s = a->saves;
    if (s->confirm) {
        if (act == ACT_LEFT || act == ACT_RIGHT) s->confirm_yes = !s->confirm_yes;
        else if (act == ACT_BACK) s->confirm = 0;
        else if (act == ACT_CONFIRM) {
            s->confirm = 0;
            if (s->confirm_yes) {
                char path[96];
                snprintf(path, sizeof path, SAVES_DIR "/%s", s->items[s->sel].dir);
                int rc = platform_rmtree(path);
                log_msg("saves: delete %s rc=%d", path, rc);
                ui_toast(a, tr(rc == 0 ? S_SV_DELETED : S_GM_DEL_FAIL));
                memmove(&s->items[s->sel], &s->items[s->sel + 1], (size_t)(s->count - s->sel - 1) * sizeof s->items[0]);
                s->count--;
                if (s->sel >= s->count && s->sel > 0) s->sel--;
            }
        }
        return;
    }
    if (act == ACT_UP && s->sel > 0) s->sel--;
    else if (act == ACT_DOWN && s->sel < s->count - 1) s->sel++;
    else if (act == ACT_MENU && s->count) { s->confirm = 1; s->confirm_yes = 0; }
    else if (act == ACT_BACK) { free(s); a->saves = NULL; return; }
    if (s->sel < s->top) s->top = s->sel;
    if (s->sel >= s->top + VISIBLE) s->top = s->sel - VISIBLE + 1;
}

static void fmt_size(long b, char *out, size_t cap) {
    if (b >= 1024 * 1024) snprintf(out, cap, "%.1f MB", (double)b / (1024.0 * 1024.0));
    else snprintf(out, cap, "%ld KB", (b + 1023) / 1024);
}

void saves_draw(const App *a) {
    const SavesScreen *s = a->saves;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    gfx_text_fit(FONT_BOLD, 28, CONTENT_Y + 6, SCREEN_W - 56, COL_TEXT, s->heading);
    int y = CONTENT_Y + 28;
    if (!s->count) gfx_text(FONT_BODY, 30, y + 10, COL_MUTED, tr(S_SV_NONE));
    for (int i = s->top; i < s->count && i < s->top + VISIBLE; i++, y += ROW_H) {
        const SaveEntry *e = &s->items[i];
        int sel = i == s->sel, x = 24, w = SCREEN_W - 58;
        gfx_round_rect(x, y, w, ROW_H - 4, 7, sel ? 0x24324aff : COL_PANEL);
        if (sel) gfx_round_rect_outline(x, y, w, ROW_H - 4, 7, COL_ACCENT);
        char right[64], size[24], name[200];
        fmt_size(e->size, size, sizeof size);
        if (s->all && e->orphan) snprintf(right, sizeof right, "%s · %s", size, tr(S_SV_ORPHAN));
        else snprintf(right, sizeof right, "%s", size);
        if (s->all) snprintf(name, sizeof name, "%s — %s", e->game, e->title);
        else snprintf(name, sizeof name, "%s", e->title);
        int rw = gfx_text_width(FONT_SMALL, right);
        gfx_text_fit(FONT_BODY, x + 12, y + 4, w - rw - 36, COL_TEXT, name);
        gfx_text(FONT_SMALL, x + w - 12 - rw, y + 6, e->orphan && s->all ? COL_WARN : COL_MUTED, right);
    }
    if (s->count > VISIBLE) {
        int track = VISIBLE * ROW_H - 4, th = track * VISIBLE / s->count, ty = (track - th) * s->top / (s->count - VISIBLE);
        gfx_round_rect(SCREEN_W - 26, CONTENT_Y + 28, 4, track, 2, 0x2a3442ff);
        gfx_round_rect(SCREEN_W - 26, CONTENT_Y + 28 + ty, 4, th, 2, COL_ACCENT);
    }
    if (s->confirm) {
        char msg[200];
        snprintf(msg, sizeof msg, tr(S_SV_DEL_Q_FMT), s->items[s->sel].title);
        int w = 360, h = 100, x = (SCREEN_W - w) / 2, cy = CONTENT_Y + (CONTENT_H - h) / 2;
        gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
        gfx_round_rect(x, cy, w, h, 12, 0x1a2230ff);
        gfx_text_fit(FONT_SMALL, x + 16, cy + 14, w - 32, COL_TEXT, msg);
        const StrId B[2] = { S_YES, S_NO };
        for (int i = 0; i < 2; i++) {
            int bx = x + w / 2 - 110 + i * 116, by = cy + h - 36, on = (i == 0) == s->confirm_yes;
            gfx_round_rect(bx, by, 104, 24, 12, on ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
            const char *t = tr(B[i]);
            gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
        }
    }
}

void saves_draw_hints(const App *a) {
    if (a->saves->count && !a->saves->confirm) {
        static const Hint h[] = { { ACT_MENU, S_HINT_DELETE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    }
}
