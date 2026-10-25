#include "ui.h"
#include "art.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

#define LEFT 14
#define TILE_W 86
#define TILE_H 48
#define TILE_GAP 8
#define SHELF_H 76
#define BANNER_H 140
#define FIRST_RUN_H 56
#define VIEW_TOP (TOP_BAR_H + 4)
#define VIEW_BOTTOM (SCREEN_H - HINT_BAR_H - 4)

static const uint32_t PALETTE[8][2] = {
    { 0x7a1d1dff, 0xd9822bff }, { 0x1d3d7aff, 0x4fb3d9ff }, { 0x2a5a2aff, 0xc9d94fff },
    { 0x4b1d7aff, 0xd94fb3ff }, { 0x333333ff, 0x8a8a8aff }, { 0x7a5a1dff, 0xf0d060ff },
    { 0x0f4a4aff, 0x3fd9c9ff }, { 0x5a1d3dff, 0xff7f7fff },
};

uint32_t ui_game_color(const Game *g, int bottom) {
    uint32_t h = 2166136261u;
    for (const char *p = g->id; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return PALETTE[h % 8][bottom ? 1 : 0];
}

const char *ui_kind_label(const Game *g) {
    switch (g->kind) {
    case GK_PSP: return tr(S_KIND_PSP);
    case GK_PS1: return tr(S_KIND_PS1);
    case GK_HOMEBREW: return tr(S_KIND_HOMEBREW);
    default: return tr(S_KIND_UTILITY);
    }
}

static const Shelf *shelves_of(const App *app, Tab tab, int *count) {
    if (tab == TAB_HOME) { *count = app->home_count; return app->home; }
    *count = app->library_count;
    return app->library;
}

int ui_focused_game(const App *app, Tab tab) {
    int count;
    const Shelf *shelves = shelves_of(app, tab, &count);
    int s = app->nav.shelf[tab], t = app->nav.tile[tab];
    if (tab == TAB_HOME && s == -1) return app->continue_game;
    if (s < 0 || s >= count || t < 0 || t >= shelves[s].count) return -1;
    return shelves[s].games[t];
}

static const char *shelf_title(const App *app, const Shelf *s) {
    switch (s->kind) {
    case SHELF_RECENT: return tr(S_SHELF_RECENT);
    case SHELF_FOLDER: return app->lib->folders[s->folder_idx].name;
    case SHELF_PSP: return tr(S_SHELF_PSP);
    case SHELF_PS1: return tr(S_SHELF_PS1);
    case SHELF_HOMEBREW: return tr(S_SHELF_HOMEBREW);
    default: return tr(S_SHELF_UTILITY);
    }
}

static float ease(float cur, float target) {
    float d = target - cur;
    return (d > -0.5f && d < 0.5f) ? target : cur + d * 0.25f;
}

static void draw_tile(const App *app, int game, int x, int y, int focused) {
    const Game *g = &app->games.items[game];
    int w = TILE_W, h = TILE_H;
    if (focused) {   // grow ~8% around the center
        x -= 4;
        y -= 2;
        w += 8;
        h += 4;
        gfx_round_rect(x - 4, y - 4, w + 8, h + 8, 8, 0x6fb0ff33);
    }
    SDL_Texture *icon = art_icon(game);
    if (icon) {
        gfx_round_rect(x, y, w, h, 5, 0x000000ff);
        gfx_texture(icon, x, y, w, h, 255);
    } else {
        gfx_gradient_v(x, y, w, h, ui_game_color(g, 0), ui_game_color(g, 1));
        gfx_text_fit(FONT_SMALL, x + 5, y + h - 16, w - 10, COL_TEXT, g->title);
    }
    if (focused) {
        gfx_round_rect_outline(x - 1, y - 1, w + 2, h + 2, 5, 0xffffffff);
        gfx_round_rect_outline(x - 2, y - 2, w + 4, h + 4, 6, 0xffffffff);
    }
}

// Full-screen artwork of the "Continue" game behind the whole Home tab (does not scroll).
static void draw_home_background(App *app) {
    const Game *g = &app->games.items[app->continue_game];
    SDL_Texture *bg = art_background(app->continue_game);
    if (bg) gfx_texture_cover(bg, 0, CONTENT_Y, SCREEN_W, CONTENT_H, 255);
    else gfx_gradient_v(0, CONTENT_Y, SCREEN_W, CONTENT_H, ui_game_color(g, 0), COL_BG);
    // Readability: soft shade behind the banner text, lighter one under the shelves.
    gfx_gradient_h(0, CONTENT_Y, 260, BANNER_H - CONTENT_Y, 0x0e131ba0, 0x0e131b00);
    gfx_gradient_v(0, BANNER_H, SCREEN_W, SCREEN_H - HINT_BAR_H - BANNER_H, 0x0e131b00, 0x0e131b70);
}

static void draw_banner(App *app, int y) {
    int game = app->continue_game;
    const Game *g = &app->games.items[game];
    int focused = app->nav.depth == 0 && app->nav.shelf[TAB_HOME] == -1;

    int x = LEFT, ty = y + 34;
    gfx_text(FONT_SMALL, x, ty, 0xffcf9fff, tr(S_CONTINUE));
    gfx_text_fit(FONT_TITLE, x, ty + 14, SCREEN_W - 2 * LEFT, COL_TEXT, g->title);
    char sub[96];
    int disc = library_last_disc(app->lib, g->id);
    if (g->disc_count > 1 && disc > 0) {
        char d[32];
        snprintf(d, sizeof d, tr(S_DISC_N), disc);
        snprintf(sub, sizeof sub, "%s · %s", ui_kind_label(g), d);
    } else {
        snprintf(sub, sizeof sub, "%s", ui_kind_label(g));
    }
    gfx_text(FONT_SMALL, x, ty + 40, 0xd8c8baff, sub);

    const char *play = tr(S_PLAY);
    int pw = gfx_text_width(FONT_BOLD, play) + 34;
    int py = ty + 58;
    if (focused) gfx_round_rect(x - 3, py - 3, pw + 6, 26, 13, 0x6fb0ff66);
    gfx_round_rect(x, py, pw, 20, 10, 0xffffffff);
    gfx_button_glyph(input_glyph(ACT_CONFIRM, app->confirm_cross), x + 12, py + 10, 13);
    gfx_text(FONT_BOLD, x + 23, py + 2, 0x111111ff, play);
}

static void draw_empty(void) {
    const char *t = tr(S_NO_GAMES), *h = tr(S_NO_GAMES_HINT);
    gfx_text(FONT_TITLE, (SCREEN_W - gfx_text_width(FONT_TITLE, t)) / 2, 110, COL_TEXT, t);
    gfx_text(FONT_SMALL, (SCREEN_W - gfx_text_width(FONT_SMALL, h)) / 2, 140, COL_MUTED, h);
}

// Home before any game was launched: invite to play (the shelves below list the games by type).
static void draw_first_run(int y) {
    gfx_text(FONT_TITLE, LEFT, y, COL_TEXT, tr(S_HOME_FIRST));
    gfx_text_fit(FONT_SMALL, LEFT, y + 26, SCREEN_W - 2 * LEFT, COL_MUTED, tr(S_HOME_FIRST_HINT));
}

void ui_shelves_draw(App *app, Tab tab) {
    int count;
    const Shelf *shelves = shelves_of(app, tab, &count);
    int banner = tab == TAB_HOME && app->continue_game >= 0;
    if (count == 0 && !banner) { draw_empty(); return; }
    int first_run = tab == TAB_HOME && !banner;

    int focus_shelf = app->nav.shelf[tab];
    int base = banner ? BANNER_H + 4 : VIEW_TOP + 4 + (first_run ? FIRST_RUN_H : 0);

    // Vertical scroll: keep the focused shelf inside the view; the banner means scroll 0.
    float target = app->scroll_y[tab];
    if (focus_shelf < 0) {
        target = 0;
    } else {
        int top = base + focus_shelf * SHELF_H;
        if (top - target < VIEW_TOP + 4) target = (float)(top - VIEW_TOP - 4);
        if (top + SHELF_H - target > VIEW_BOTTOM) target = (float)(top + SHELF_H - VIEW_BOTTOM);
        if (target < 0) target = 0;
    }
    app->scroll_y[tab] = ease(app->scroll_y[tab], target);
    int sy = (int)app->scroll_y[tab];

    if (banner) {
        draw_home_background(app);
        draw_banner(app, -sy);
    }
    if (first_run) draw_first_run(VIEW_TOP + 8 - sy);

    for (int s = 0; s < count; s++) {
        int y = base + s * SHELF_H - sy;
        if (y > SCREEN_H || y + SHELF_H < 0) continue;
        const Shelf *sh = &shelves[s];
        int focused_shelf = app->nav.depth == 0 && s == focus_shelf;

        // Horizontal scroll: keep the focused tile fully visible.
        float tx = app->scroll_x[tab][s];
        if (s == focus_shelf) {
            int tile_x = app->nav.tile[tab] * (TILE_W + TILE_GAP);
            if (tile_x - tx < 0) tx = (float)tile_x;
            if (tile_x + TILE_W - tx > SCREEN_W - 2 * LEFT) tx = (float)(tile_x + TILE_W - (SCREEN_W - 2 * LEFT));
        }
        app->scroll_x[tab][s] = ease(app->scroll_x[tab][s], tx);
        int sx = (int)app->scroll_x[tab][s];

        char title[160];
        snprintf(title, sizeof title, "%s  %d", shelf_title(app, sh), sh->count);
        gfx_text(FONT_SMALL, LEFT, y, focused_shelf ? COL_TEXT : COL_MUTED, title);
        if (focused_shelf && app->nav.tile[tab] < sh->count) {
            const Game *g = &app->games.items[sh->games[app->nav.tile[tab]]];
            int tw = gfx_text_width(FONT_SMALL, title) + 12;
            gfx_text_fit(FONT_BOLD, LEFT + tw, y - 2, SCREEN_W - LEFT * 2 - tw, COL_ACCENT, g->title);
        }
        int ty = y + 18;
        if (sh->count == 0) {
            gfx_round_rect_outline(LEFT, ty, TILE_W, TILE_H, 5, focused_shelf ? COL_ACCENT : 0x46536aff);
            gfx_text_fit(FONT_SMALL, LEFT + TILE_W + 12, ty + 16, SCREEN_W - LEFT * 2 - TILE_W - 12, COL_MUTED,
                         tr(S_FOLDER_EMPTY));
        }
        for (int t = 0; t < sh->count; t++) {
            int x = LEFT + t * (TILE_W + TILE_GAP) - sx;
            if (x > SCREEN_W || x + TILE_W < 0) continue;
            int focused = focused_shelf && t == app->nav.tile[tab];
            if (!focused) draw_tile(app, sh->games[t], x, ty, 0);
        }
        if (focused_shelf && app->nav.tile[tab] < sh->count) {   // focused tile on top
            int t = app->nav.tile[tab];
            draw_tile(app, sh->games[t], LEFT + t * (TILE_W + TILE_GAP) - sx, ty, 1);
        }
    }
}
