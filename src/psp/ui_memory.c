// "About PSP › Memory" screens (1.2.6): what uses the storage (counted only on the button) and details
// (file system, RAM, flash, UMD). A PSP Go shows its internal storage (ef0:) first, the card after it.
// View only: nothing here deletes or writes.
#include "ui.h"
#include "gfx.h"
#include "gamesize.h"
#include "memory_hw.h"
#include "memstat.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DET_MAX 32
#define LINE_H 15
#define MAX_DEV 2

typedef struct { int header; char label[64]; char value[48]; } DetRow;   // UTF-8: Cyrillic takes 2 bytes a letter

typedef struct MemoryScreen {
    MemScreenKind kind;
    const char *dev[MAX_DEV];   // storages in display order: "ef0:" (PSP Go) and/or "ms0:"
    MemCard card[MAX_DEV];
    int n_dev, cur;             // cur: the storage shown on "What uses the space"
    int top;                    // first visible list row
    DetRow rows[DET_MAX];       // details
    int n_rows, ram_row;
    uint32_t ram_ms, card_ms;
} MemoryScreen;

static const StrId CAT_NAME[MC_COUNT] = { S_MEM_CAT_SYS, S_MEM_CAT_ISO, S_MEM_CAT_DLC, S_MEM_CAT_GAME, S_MEM_CAT_SAVE,
                                          S_MEM_CAT_MUSIC, S_MEM_CAT_PHOTO, S_MEM_CAT_VIDEO, S_MEM_CAT_OTHER };
static const uint32_t CAT_COL[MC_COUNT] = { 0x8c9bb0ff, 0x6fb0ffff, 0xb38cffff, 0x3fae84ff, 0xe0b45aff,
                                            0xff7a8aff, 0x4fd1d9ff, 0xff9f43ff, 0x5b6779ff };
#define COL_TRACK 0x2a3442ff

static int lang_en(void) { return i18n_get() == LANG_EN; }

static void size_text(char *out, size_t cap, long long bytes) { game_size_text(out, cap, bytes, lang_en()); }

static int is_internal(const char *dev) { return strncmp(dev, "ef0", 3) == 0; }
static StrId dev_name(const char *dev) { return is_internal(dev) ? S_MEM_INTERNAL : S_MEM_CARD; }

// ---------- details ----------

static void add_row(MemoryScreen *s, int header, const char *label, const char *value) {
    if (s->n_rows >= DET_MAX) return;
    DetRow *r = &s->rows[s->n_rows++];
    r->header = header;
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->value, sizeof r->value, "%s", value ? value : "");
}

static void ram_value(char *out, size_t cap, int which) {
    uint32_t free_b, total_b;
    platform_memory(&free_b, &total_b);
    uint32_t v = which ? free_b : total_b;
    if (!v) { out[0] = '\0'; return; }
    size_text(out, cap, v);
}

static void storage_rows(MemoryScreen *s, int i) {
    char v[48];
    const MemCard *c = &s->card[i];
    const char *dev = s->dev[i];
    if (!c->ok) return;   // unreadable: no rows rather than zeros
    add_row(s, 1, tr(dev_name(dev)), NULL);
    size_text(v, sizeof v, c->total), add_row(s, 0, tr(S_MEM_TOTAL), v);
    size_text(v, sizeof v, c->total - c->free), add_row(s, 0, tr(S_MEM_USED), v);
    size_text(v, sizeof v, c->free), add_row(s, 0, tr(S_MEM_FREE), v);
    MemFs fs = MFS_UNKNOWN;
    char label[12] = "";
    if (is_internal(dev) || memory_card_boot(&fs, label) != 0) fs = memstat_fs_from_clusters(c->clusters);
    if (fs != MFS_UNKNOWN) add_row(s, 0, tr(S_MEM_FS), memstat_fs_name(fs));
    if (c->cluster) {
        if (c->cluster < 1024) snprintf(v, sizeof v, lang_en() ? "%u B" : "%u Б", (unsigned)c->cluster);
        else snprintf(v, sizeof v, lang_en() ? "%u KB" : "%u КБ", (unsigned)(c->cluster / 1024));
        add_row(s, 0, tr(S_MEM_CLUSTER), v);
    }
    if (label[0]) add_row(s, 0, tr(S_MEM_LABEL), label);
    MemTotals t;
    if (memory_scan_state(dev, &t, NULL, 0) == 1) {
        snprintf(v, sizeof v, "%ld", t.files);
        add_row(s, 0, tr(S_MEM_FILES), v);
    }
}

static void build_details(App *a, MemoryScreen *s) {
    char v[48], u[24], w[24];
    s->n_rows = 0;
    for (int i = 0; i < s->n_dev; i++) storage_rows(s, i);

    add_row(s, 1, tr(S_MEM_RAM), NULL);
    ram_value(v, sizeof v, 0);
    if (v[0]) add_row(s, 0, tr(S_MEM_RAM_APPS), v);
    ram_value(v, sizeof v, 1);
    s->ram_row = v[0] ? s->n_rows : -1;
    if (v[0]) add_row(s, 0, tr(S_MEM_RAM_FREE), v);
    settings_ark_value_text(a, SID_HIGHMEM, v, sizeof v);
    if (v[0]) add_row(s, 0, tr(S_MEM_HIGHMEM), v);

    int flash_head = 0;
    for (int i = 0; i < 4; i++) {
        char dev[8];
        MemCard f;
        snprintf(dev, sizeof dev, "flash%d:", i);
        if (memory_card_read(dev, &f) != 0) continue;
        if (!flash_head) add_row(s, 1, tr(S_MEM_FLASH), NULL), flash_head = 1;
        size_text(u, sizeof u, f.free);
        size_text(w, sizeof w, f.total);
        snprintf(v, sizeof v, tr(S_MEM_OF_FMT), u, w);
        add_row(s, 0, dev, v);
    }
    long long umd = memory_umd_size();
    if (umd > 0) {
        add_row(s, 1, tr(S_MEM_UMD), NULL);
        size_text(v, sizeof v, umd), add_row(s, 0, tr(S_MEM_UMD_SIZE), v);
    }
}

static int det_visible(void) { return (CONTENT_H - 34) / LINE_H; }

static void draw_details(const MemoryScreen *s) {
    gfx_text(FONT_BOLD, 16, CONTENT_Y + 6, COL_TEXT, tr(S_MEM_DETAILS_TITLE));
    int x = 16, w = SCREEN_W - 32, y = CONTENT_Y + 30, vis = det_visible();
    for (int i = s->top; i < s->n_rows && i < s->top + vis; i++, y += LINE_H) {
        const DetRow *r = &s->rows[i];
        if (r->header) { gfx_text(FONT_SMALL, x, y, COL_ACCENT, r->label); continue; }
        int vw = gfx_text_width(FONT_SMALL, r->value);
        gfx_text_fit(FONT_SMALL, x + 10, y, w - 10 - vw - 12, COL_MUTED, r->label);
        gfx_text(FONT_SMALL, x + w - vw, y, COL_TEXT, r->value);
    }
}

// ---------- what uses the space ----------

#define USAGE_LIST_Y (CONTENT_Y + 92)

static int usage_visible(void) { return (CONTENT_Y + CONTENT_H - 16 - USAGE_LIST_Y) / LINE_H; }

static void draw_usage(const MemoryScreen *s) {
    const char *dev = s->dev[s->cur];
    const MemCard *c = &s->card[s->cur];
    char v[48], p[16], line[96];
    int en = lang_en();
    // Title and capacity share one line: same font and baseline.
    gfx_text(FONT_BOLD, 16, CONTENT_Y + 6, COL_TEXT, tr(dev_name(dev)));
    if (c->ok) {
        size_text(v, sizeof v, c->total);
        gfx_text(FONT_BOLD, SCREEN_W - 16 - gfx_text_width(FONT_BOLD, v), CONTENT_Y + 6, COL_MUTED, v);
    }
    MemTotals t;
    char cur[64];
    int st = memory_scan_state(dev, &t, cur, sizeof cur);
    MemCat order[MC_COUNT];
    int n = st >= 0 ? memstat_order(&t, order) : 0;
    if (st == 0) {   // counting: "Other" is not known yet
        int k = 0;
        for (int i = 0; i < n; i++) if (order[i] != MC_OTHER) order[k++] = order[i];
        n = k;
    }

    int bx = 16, bw = SCREEN_W - 32, by = CONTENT_Y + 32;
    gfx_round_rect(bx, by, bw, 12, 3, COL_TRACK);
    if (c->ok && c->total > 0) {
        if (n == 0) {   // no count yet: used / free only
            gfx_round_rect(bx, by, (int)((c->total - c->free) * bw / c->total), 12, 3, COL_ACCENT);
        } else {
            int x = bx;
            for (int i = 0; i < n; i++) {
                int sw = (int)(t.bytes[order[i]] * bw / c->total);
                if (sw < 1) sw = 1;
                if (x + sw > bx + bw) sw = bx + bw - x;
                if (sw > 0) gfx_rect(x, by, sw, 12, CAT_COL[order[i]]);
                x += sw;
            }
        }
        size_text(v, sizeof v, c->free);
        memstat_pct_text(p, sizeof p, c->free, c->total, en);
        snprintf(line, sizeof line, tr(S_MEM_FREE_FMT), v, p);
        gfx_text(FONT_SMALL, 16, by + 16, COL_TEXT, line);
    }

    int busy = memory_scan_busy();
    int bty = CONTENT_Y + 64, btw = 220;   // the only action on the screen: ✕/○ presses it
    gfx_round_rect(16, bty, btw, 22, 6, busy ? COL_PANEL : 0x24324aff);
    if (!busy) gfx_round_rect_outline(16, bty, btw, 22, 6, COL_ACCENT);
    const char *bt = tr(st == 1 ? S_MEM_RECOUNT : S_MEM_COUNT_BTN);
    gfx_text_fit(FONT_BODY, 26, bty + 4, btw - 20, busy ? COL_MUTED : COL_TEXT, bt);

    int y = USAGE_LIST_Y, vis = usage_visible();
    for (int i = s->top; i < n && i < s->top + vis; i++, y += LINE_H) {
        MemCat k = order[i];
        gfx_rect(16, y + 4, 8, 8, CAT_COL[k]);
        gfx_text(FONT_SMALL, 30, y, COL_TEXT, tr(CAT_NAME[k]));
        size_text(v, sizeof v, t.bytes[k]);
        if (c->ok) {   // shares of an unknown capacity would all read "0 %"
            memstat_pct_text(p, sizeof p, t.bytes[k], c->total, en);
            gfx_text(FONT_SMALL, SCREEN_W - 16 - gfx_text_width(FONT_SMALL, p), y, COL_MUTED, p);
        }
        gfx_text(FONT_SMALL, SCREEN_W - 70 - gfx_text_width(FONT_SMALL, v), y, COL_TEXT, v);
    }
    if (st == 0) {
        snprintf(line, sizeof line, "%s %s", tr(S_MEM_COUNTING), cur);
        gfx_text_fit(FONT_TINY, 16, CONTENT_Y + CONTENT_H - 14, SCREEN_W - 32, COL_MUTED, line);
    }
}

static int usage_rows(const MemoryScreen *s) {
    MemTotals t;
    MemCat order[MC_COUNT];
    return memory_scan_state(s->dev[s->cur], &t, NULL, 0) >= 0 ? memstat_order(&t, order) : 0;
}

// ---------- public ----------

static void read_storages(MemoryScreen *s) {
    s->n_dev = 0;
    static const char *const ON_GO[] = { "ef0:", "ms0:" }, *const ON_PSP[] = { "ms0:" };
    int go = platform_model() == 4;
    const char *const *list = go ? ON_GO : ON_PSP;
    int n = go ? 2 : 1;
    for (int i = 0; i < n; i++) {
        MemCard c;
        if (memory_card_read(list[i], &c) != 0 && s->n_dev) continue;   // the first is always listed
        s->dev[s->n_dev] = list[i];
        s->card[s->n_dev++] = c;
    }
}

void memory_open(App *a, MemScreenKind k) {
    if (!a->memory) a->memory = calloc(1, sizeof *a->memory);
    if (!a->memory) return;
    MemoryScreen *s = a->memory;
    s->kind = k;
    s->top = 0;
    s->cur = 0;
    read_storages(s);   // one devctl per storage: shown at once; the count waits for the button
    s->card_ms = a->now_ms;
    if (k == MEMS_DETAILS) {
        build_details(a, s);
        s->ram_ms = a->now_ms;
    }
}

static void close_screen(App *a) {
    memory_scan_stop();
    free(a->memory);
    a->memory = NULL;
}

void memory_action(App *a, Action act) {
    MemoryScreen *s = a->memory;
    if (!s) return;
    if (act == ACT_BACK || act == ACT_HOME) { close_screen(a); return; }
    int total = s->kind == MEMS_DETAILS ? s->n_rows : usage_rows(s);
    int vis = s->kind == MEMS_DETAILS ? det_visible() : usage_visible();
    if (act == ACT_UP && s->top > 0) s->top--;
    if (act == ACT_DOWN && s->top + vis < total) s->top++;
    if (s->kind != MEMS_USAGE) return;
    if ((act == ACT_LEFT || act == ACT_RIGHT) && s->n_dev > 1) {   // PSP Go: internal storage / card
        s->cur = (s->cur + 1) % s->n_dev;
        s->top = 0;
    }
    if (act == ACT_CONFIRM && !memory_scan_busy()) {
        s->top = 0;
        memory_scan_start(s->dev[s->cur]);
    }
}

void memory_tick(App *a) {
    MemoryScreen *s = a->memory;
    if (!s) return;
    if (s->kind == MEMS_DETAILS && s->ram_row >= 0 && a->now_ms - s->ram_ms >= 1000) {
        ram_value(s->rows[s->ram_row].value, sizeof s->rows[s->ram_row].value, 1);
        s->ram_ms = a->now_ms;
    }
    if (s->kind == MEMS_USAGE && a->now_ms - s->card_ms >= 2000) {   // keep "Free" honest while files change
        memory_card_read(s->dev[s->cur], &s->card[s->cur]);
        s->card_ms = a->now_ms;
    }
}

void memory_draw(const App *a) {
    const MemoryScreen *s = a->memory;
    if (!s) return;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    if (s->kind == MEMS_DETAILS) draw_details(s);
    else draw_usage(s);
}

void memory_draw_hints(const App *a) {
    const MemoryScreen *s = a->memory;
    if (!s) return;
    if (s->kind == MEMS_USAGE && s->n_dev > 1) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_START }, { ACT_RIGHT, S_MEM_SWITCH }, { ACT_UP, S_HINT_ROW },
                                  { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 4);
    } else if (s->kind == MEMS_USAGE) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_START }, { ACT_UP, S_HINT_ROW }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 3);
    } else {
        static const Hint h[] = { { ACT_UP, S_HINT_ROW }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    }
}

void memory_free_text(const char *dev, char *out, size_t cap) {
    static uint32_t last[MAX_DEV];
    static MemCard c[MAX_DEV];
    int i = is_internal(dev);
    uint32_t now = platform_ms();
    if (!last[i] || now - last[i] >= 2000) { memory_card_read(dev, &c[i]); last[i] = now; }   // drawn every frame
    if (!c[i].ok) { out[0] = '\0'; return; }
    char f[24], t[24];
    size_text(f, sizeof f, c[i].free);
    size_text(t, sizeof t, c[i].total);
    snprintf(out, cap, tr(S_MEM_OF_FMT), f, t);
}
