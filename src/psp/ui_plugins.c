#include "ui.h"
#include "gfx.h"
#include "log.h"
#include "platform.h"
#include "plugins.h"
#include "plugdesc.h"
#include "safefile.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Settings › Plugin manager: ARK's three PLUGINS.TXT (ARK folder, ms0:/SEPLUGINS, ef0:/SEPLUGINS),
// like ARK's own plugin manager (XMBControl plugins.c).
#define PLACES 3
#define ROWS_MAX (PLACES * PLUGINS_MAX + 1)
#define FOUND_MAX 48
#define ROW_H 26
#define VISIBLE 7

typedef enum { PM_LIST, PM_LEVEL, PM_DELETE, PM_FILES, PM_NEW_LEVEL } PlugMode;

typedef struct PluginsScreen {
    PluginFile files[PLACES];
    char paths[PLACES][160];
    int place_ok[PLACES];
    int row_place[ROWS_MAX], row_line[ROWS_MAX], rows;   // last row = "Install…"
    int sel, top, mode, pick, confirm_yes;
    char found[FOUND_MAX][160];
    int found_place[FOUND_MAX], found_count, found_sel, found_top;
} PluginsScreen;

static const char *const PLACE_NAMES[PLACES] = { "ARK", "SEPLUGINS", "Go" };

static void build_rows(PluginsScreen *s) {
    s->rows = 0;
    for (int p = 0; p < PLACES; p++)
        for (int i = 0; i < s->files[p].count; i++) {
            const PluginLine *l = &s->files[p].lines[i];
            if (l->custom || l->removed) continue;
            s->row_place[s->rows] = p;
            s->row_line[s->rows] = i;
            s->rows++;
        }
    s->row_place[s->rows] = -1;                   // "Install a plugin…"
    s->rows++;
    if (s->sel >= s->rows) s->sel = s->rows - 1;
}

static void load(PluginsScreen *s) {
    snprintf(s->paths[0], sizeof s->paths[0], "%sPLUGINS.TXT", platform_arkpath());
    snprintf(s->paths[1], sizeof s->paths[1], "ms0:/SEPLUGINS/PLUGINS.TXT");
    snprintf(s->paths[2], sizeof s->paths[2], "ef0:/SEPLUGINS/PLUGINS.TXT");
    // ARK skips its own folder's file when the ARK folder is SEPLUGINS itself.
    s->place_ok[0] = strncasecmp(platform_arkpath() + 5, "seplugins", 9) != 0;
    for (int p = 0; p < PLACES; p++) {
        int missing = 0;
        char *t = s->place_ok[p] || p ? safe_read_text(s->paths[p], 64 * 1024, &missing) : NULL;
        if (p) s->place_ok[p] = t != NULL || missing;
        plugins_parse(t ? t : "", &s->files[p]);
        free(t);
    }
    build_rows(s);
}

static int save(App *a, PluginsScreen *s, int place) {
    char *t = plugins_render(&s->files[place]);
    if (!t) return -1;
    if (place) sceIoMkdir(place == 1 ? "ms0:/SEPLUGINS" : "ef0:/SEPLUGINS", 0777);
    int rc = safe_write_text(s->paths[place], t);
    log_msg("plugins: write %s rc=%d", s->paths[place], rc);
    free(t);
    ui_toast(a, tr(rc == 0 ? S_ST_APPLY_RESTART : S_ST_REG_FAILED));
    return rc;
}

static void scan_dir(PluginsScreen *s, int place, const char *dir, int depth) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0 && s->found_count < FOUND_MAX) {
        char path[320];   // dir (up to 160) + a 255-char name would not fit 160
        if (e.d_name[0] == '.') { memset(&e, 0, sizeof e); continue; }
        snprintf(path, sizeof path, "%s/%s", dir, e.d_name);
        if (FIO_S_ISDIR(e.d_stat.st_mode)) {
            if (depth == 0 && strcasecmp(e.d_name, "lib") != 0) scan_dir(s, place, path, 1);
        } else {
            size_t n = strlen(e.d_name);
            if (n > 4 && strcasecmp(e.d_name + n - 4, ".prx") == 0 && !plugins_blacklisted(e.d_name) &&
                !plugins_has(&s->files[place], e.d_name)) {
                size_t len = strlen(path);
                if (len < sizeof s->found[0]) {   // a longer path would not fit PLUGINS.TXT either: skip it
                    memcpy(s->found[s->found_count], path, len + 1);
                    s->found_place[s->found_count++] = place;
                }
            }
        }
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
}

void plugins_open(App *a) {
    PluginsScreen *s = calloc(1, sizeof *s);
    if (!s) return;
    load(s);
    a->plug = s;
    log_msg("plugins: %d in the lists", s->rows - 1);
}

static void close_screen(App *a) {
    free(a->plug);
    a->plug = NULL;
}

static int move(int *sel, int *top, int count, Action act) {
    if (act == ACT_UP && *sel > 0) (*sel)--;
    else if (act == ACT_DOWN && *sel < count - 1) (*sel)++;
    else return 0;
    if (*sel < *top) *top = *sel;
    if (*sel >= *top + VISIBLE) *top = *sel - VISIBLE + 1;
    return 1;
}

void plugins_action(App *a, Action act) {
    PluginsScreen *s = a->plug;
    switch (s->mode) {
    case PM_LEVEL: case PM_NEW_LEVEL:
        if (act == ACT_UP && s->pick > 0) s->pick--;
        else if (act == ACT_DOWN && s->pick < PLUGIN_LEVEL_COUNT - 1) s->pick++;
        else if (act == ACT_BACK) s->mode = s->mode == PM_LEVEL ? PM_LIST : PM_FILES;
        else if (act == ACT_CONFIRM) {
            if (s->mode == PM_LEVEL) {
                int p = s->row_place[s->sel];
                plugins_set_level(&s->files[p].lines[s->row_line[s->sel]], s->pick);
                save(a, s, p);
            } else {
                int p = s->found_place[s->found_sel];
                const char *path = s->found[s->found_sel];
                if (plugins_add(&s->files[p], path, s->pick) == 0 && save(a, s, p) == 0)
                    log_msg("plugins: installed %s level %d", path, s->pick);
                build_rows(s);
            }
            s->mode = PM_LIST;
        }
        return;
    case PM_DELETE:
        if (act == ACT_LEFT || act == ACT_RIGHT) s->confirm_yes = !s->confirm_yes;
        else if (act == ACT_BACK) s->mode = PM_LIST;
        else if (act == ACT_CONFIRM) {
            if (s->confirm_yes) {
                int p = s->row_place[s->sel];
                s->files[p].lines[s->row_line[s->sel]].removed = 1;
                save(a, s, p);
                build_rows(s);
            }
            s->mode = PM_LIST;
        }
        return;
    case PM_FILES:
        if (move(&s->found_sel, &s->found_top, s->found_count, act)) return;
        if (act == ACT_BACK) s->mode = PM_LIST;
        else if (act == ACT_CONFIRM && s->found_count) { s->pick = 1; s->mode = PM_NEW_LEVEL; }
        return;
    default: break;
    }
    if (move(&s->sel, &s->top, s->rows, act)) return;
    int install = s->row_place[s->sel] < 0;
    if (act == ACT_BACK) close_screen(a);
    else if (act == ACT_CONFIRM && install) {
        s->found_count = s->found_sel = s->found_top = 0;
        scan_dir(s, 1, "ms0:/SEPLUGINS", 0);
        scan_dir(s, 2, "ef0:/SEPLUGINS", 0);
        if (s->found_count) s->mode = PM_FILES;
        else ui_toast(a, tr(S_PL_NONE_FOUND));
    } else if (act == ACT_CONFIRM) {
        int lv = plugins_level_index(&s->files[s->row_place[s->sel]].lines[s->row_line[s->sel]]);
        s->pick = lv < 0 ? 2 : lv;               // a per-game line starts the list at "Games"
        s->mode = PM_LEVEL;
    } else if (act == ACT_MENU && !install) {
        s->confirm_yes = 0;
        s->mode = PM_DELETE;
    }
}

static void level_text(const PluginLine *l, char *out, size_t cap) {
    int lv = plugins_level_index(l);
    if (lv >= 0) snprintf(out, cap, "%s", tr((StrId)(S_PL_LV0 + lv)));
    else {
        char g[64];
        snprintf(g, sizeof g, tr(S_PL_GAMES_FMT), l->runlevel);
        snprintf(out, cap, "%s%s", g, l->on ? "" : " · off");
    }
}

static void draw_list_box(const char *title, int count, int sel, int top, int w, const char *(*label)(const App *, int, char *, size_t),
                          const App *a, int accent) {
    int h = 34 + (count < VISIBLE ? count : VISIBLE) * 22 + 8;
    int x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000090);
    gfx_round_rect(x, y, w, h, 10, 0x1a2230fa);
    gfx_round_rect_outline(x, y, w, h, 10, COL_ACCENT);
    gfx_text(FONT_BOLD, x + 14, y + 9, COL_TEXT, title);
    for (int i = top; i < count && i < top + VISIBLE; i++) {
        int ry = y + 34 + (i - top) * 22;
        if (i == sel) gfx_round_rect(x + 6, ry, w - 12, 20, 6, 0x2b5fa8ff);
        char buf[160];
        gfx_text_fit(FONT_BODY, x + 16, ry + 2, w - 32, i == accent ? COL_ACCENT : COL_TEXT, label(a, i, buf, sizeof buf));
    }
}

static const char *level_label(const App *a, int i, char *buf, size_t cap) {
    (void)a;
    snprintf(buf, cap, "%s", tr((StrId)(S_PL_LV0 + i)));
    return buf;
}

static const char *file_label(const App *a, int i, char *buf, size_t cap) {
    const PluginsScreen *s = a->plug;
    snprintf(buf, cap, "%s", s->found[i] + (strncmp(s->found[i], "ms0:/SEPLUGINS/", 15) == 0 ? 15 : 0));
    return buf;
}

void plugins_draw(const App *a) {
    const PluginsScreen *s = a->plug;
    gfx_text(FONT_BOLD, 28, CONTENT_Y + 6, COL_TEXT, tr(S_ST_PLUGINS));
    int y = CONTENT_Y + 28;
    if (s->rows == 1) gfx_text(FONT_SMALL, 30, y + ROW_H * 1 + 6, COL_MUTED, tr(S_PL_EMPTY));
    for (int i = s->top; i < s->rows && i < s->top + VISIBLE; i++, y += ROW_H) {
        int sel = i == s->sel, x = 24, w = SCREEN_W - 58;
        gfx_round_rect(x, y, w, ROW_H - 4, 7, sel ? 0x24324aff : COL_PANEL);
        if (sel) gfx_round_rect_outline(x, y, w, ROW_H - 4, 7, COL_ACCENT);
        if (s->row_place[i] < 0) {
            gfx_text(FONT_BODY, x + 14, y + 4, COL_ACCENT, tr(S_PL_INSTALL));
            continue;
        }
        const PluginLine *l = &s->files[s->row_place[i]].lines[s->row_line[i]];
        char name[96], lv[96], right[128];
        plugins_name(l, name, sizeof name);
        level_text(l, lv, sizeof lv);
        snprintf(right, sizeof right, "%s · %s", lv, PLACE_NAMES[s->row_place[i]]);
        int rw = gfx_text_width(FONT_SMALL, right);
        gfx_text_fit(FONT_BODY, x + 14, y + 4, w - rw - 40, plugins_level_index(l) == 0 ? COL_MUTED : COL_TEXT, name);
        gfx_text(FONT_SMALL, x + w - 12 - rw, y + 6, sel ? COL_ACCENT : COL_MUTED, right);
        if (sel)   // what the selected plugin does, under the list (1.2.4)
            gfx_text_fit(FONT_SMALL, 30, CONTENT_Y + 28 + VISIBLE * ROW_H + 2, SCREEN_W - 60, COL_MUTED,
                         plugdesc_get(name, i18n_get() == LANG_EN));
    }
    if (s->rows > VISIBLE) {
        int track = VISIBLE * ROW_H - 4, th = track * VISIBLE / s->rows;
        int ty = (track - th) * s->top / (s->rows - VISIBLE);
        gfx_round_rect(SCREEN_W - 26, CONTENT_Y + 28, 4, track, 2, 0x2a3442ff);
        gfx_round_rect(SCREEN_W - 26, CONTENT_Y + 28 + ty, 4, th, 2, COL_ACCENT);
    }
    if (s->mode == PM_LEVEL || s->mode == PM_NEW_LEVEL) {
        int cur = -1;
        if (s->mode == PM_LEVEL) cur = plugins_level_index(&s->files[s->row_place[s->sel]].lines[s->row_line[s->sel]]);
        draw_list_box(tr(S_PL_CHOOSE_LEVEL), PLUGIN_LEVEL_COUNT, s->pick, 0, 200, level_label, a, cur);
    } else if (s->mode == PM_FILES) {
        draw_list_box(tr(S_PL_CHOOSE_FILE), s->found_count, s->found_sel, s->found_top, 340, file_label, a, -1);
    } else if (s->mode == PM_DELETE) {
        const PluginLine *l = &s->files[s->row_place[s->sel]].lines[s->row_line[s->sel]];
        char name[96], msg[200];
        plugins_name(l, name, sizeof name);
        snprintf(msg, sizeof msg, tr(S_PL_DELETE_FMT), name);
        int w = 360, h = 100, x = (SCREEN_W - w) / 2, cy = CONTENT_Y + (CONTENT_H - h) / 2;
        gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
        gfx_round_rect(x, cy, w, h, 12, 0x1a2230ff);
        gfx_text_fit(FONT_SMALL, x + 16, cy + 14, w - 32, COL_TEXT, msg);
        const StrId B[2] = { S_YES, S_NO };
        for (int i = 0; i < 2; i++) {
            int bx = x + w / 2 - 110 + i * 116, by = cy + h - 36;
            int on = (i == 0) == s->confirm_yes;
            gfx_round_rect(bx, by, 104, 24, 12, on ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
            const char *t = tr(B[i]);
            gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
        }
    }
}

void plugins_draw_hints(const App *a) {
    const PluginsScreen *s = a->plug;
    if (s->mode == PM_LIST && s->row_place[s->sel] >= 0) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_MENU, S_HINT_DELETE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 3);
    } else {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    }
}
