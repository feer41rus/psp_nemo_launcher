#include "ui.h"
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

// Read-only text pages (About › Creators / Beta testers / Thanks): wrapped once when opened
// (text is not measured every frame), ↑↓ scroll, ○ back.
#define PAGE_LINES_MAX 400
#define LINE_H 16
#define PAGE_W (SCREEN_W - 56)

typedef struct TextPage {
    StrId title;
    char *lines[PAGE_LINES_MAX];
    int bold[PAGE_LINES_MAX];
    int count, top;
} TextPage;

static int visible_lines(void) { return (CONTENT_H - 34) / LINE_H; }

static void add_line(TextPage *p, const char *s, size_t n, int bold) {
    if (p->count >= PAGE_LINES_MAX) return;
    char *l = malloc(n + 1);
    if (!l) return;
    memcpy(l, s, n);
    l[n] = '\0';
    p->bold[p->count] = bold;
    p->lines[p->count++] = l;
}

// One source line wrapped on spaces to PAGE_W. Lines starting with "# " are headings.
static void wrap(TextPage *p, const char *s, size_t len) {
    int bold = len >= 2 && s[0] == '#' && s[1] == ' ';
    if (bold) { s += 2; len -= 2; }
    FontId f = bold ? FONT_BOLD : FONT_SMALL;
    if (!len) { add_line(p, "", 0, 0); return; }
    char buf[256];
    while (len) {
        size_t best = 0, n = 0;
        while (n < len) {
            size_t next = n;
            while (next < len && s[next] != ' ') next++;
            if (next >= sizeof buf) break;
            memcpy(buf, s, next);
            buf[next] = '\0';
            if (gfx_text_width(f, buf) > PAGE_W && best) break;
            best = next;
            if (next >= len) break;
            n = next + 1;
        }
        if (!best) best = len < sizeof buf - 1 ? len : sizeof buf - 1;
        add_line(p, s, best, bold);
        s += best;
        len -= best;
        while (len && *s == ' ') { s++; len--; }
    }
}

void textpage_open(App *a, StrId title, const char *text) {
    TextPage *p = calloc(1, sizeof *p);
    if (!p) return;
    p->title = title;
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t n = nl ? (size_t)(nl - line) : strlen(line);
        if (n && line[n - 1] == '\r') n--;
        wrap(p, line, n);
        if (!nl) break;
        line = nl + 1;
    }
    a->page = p;
}

static void close_page(App *a) {
    TextPage *p = a->page;
    for (int i = 0; i < p->count; i++) free(p->lines[i]);
    free(p);
    a->page = NULL;
}

int textpage_action(App *a, Action act) {
    TextPage *p = a->page;
    if (!p) return 0;
    int max_top = p->count - visible_lines();
    if (max_top < 0) max_top = 0;
    if (act == ACT_DOWN && p->top < max_top) p->top++;
    else if (act == ACT_UP && p->top > 0) p->top--;
    else if (act == ACT_RIGHT) p->top = p->top + visible_lines() < max_top ? p->top + visible_lines() : max_top;
    else if (act == ACT_LEFT) p->top = p->top > visible_lines() ? p->top - visible_lines() : 0;
    else if (act == ACT_BACK || act == ACT_CONFIRM) close_page(a);
    return 1;
}

void textpage_draw(const App *a) {
    const TextPage *p = a->page;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    gfx_text(FONT_BOLD, 28, CONTENT_Y + 6, COL_TEXT, tr(p->title));
    int y = CONTENT_Y + 30, vis = visible_lines();
    for (int i = p->top; i < p->count && i < p->top + vis; i++, y += LINE_H)
        gfx_text(p->bold[i] ? FONT_BOLD : FONT_SMALL, 28, y, p->bold[i] ? COL_ACCENT : COL_TEXT, p->lines[i]);
    if (p->count > vis) {
        int track = vis * LINE_H, th = track * vis / p->count;
        int ty = (track - th) * p->top / (p->count - vis);
        gfx_round_rect(SCREEN_W - 20, CONTENT_Y + 30, 4, track, 2, 0x2a3442ff);
        gfx_round_rect(SCREEN_W - 20, CONTENT_Y + 30 + ty, 4, th, 2, COL_ACCENT);
    }
}
