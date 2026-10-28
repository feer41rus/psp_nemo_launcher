#include "ui.h"
#include "gamesize.h"
#include "art.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

// A rounded, half see-through bubble in Nemo's blue with white text (1.2.5: game format/size, "DLC: installed").
static void bubble(int x, int y, const char *text) {
    int w = gfx_text_width(FONT_SMALL, text) + 16;
    gfx_round_rect(x, y, w, 18, 9, 0x6fb0ff80);
    gfx_text(FONT_SMALL, x + 8, y + 2, 0xffffffff, text);
}

void ui_card_draw(App *app) {
    int game = app->nav.card_game;
    if (game < 0 || game >= app->games.count) return;
    const Game *g = &app->games.items[game];

    // Artwork across the content area (between the bars); it runs behind the thumbnail and
    // fades out on the left and just above the Play button. The opening animation is off until
    // the "card stuck at t=0" report is understood (see timing lines in nemo.log).
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    SDL_Texture *bg = art_background(game);
    if (bg) gfx_texture_cover(bg, 0, CONTENT_Y, SCREEN_W, CONTENT_H, 255);
    else gfx_gradient_v(0, CONTENT_Y, SCREEN_W, CONTENT_H, ui_game_color(g, 0), COL_BG);
    gfx_gradient_h(0, CONTENT_Y, 300, CONTENT_H, 0x0e131bd8, 0x0e131b00);
    gfx_gradient_v(0, SCREEN_H - HINT_BAR_H - 70, SCREEN_W, 70, 0x0e131b00, 0x0e131bc8);

    int x = 24, y = TOP_BAR_H + 18;
    SDL_Texture *icon = art_icon(game);
    if (icon) {
        gfx_texture(icon, x, y, 144, 80, 255);
    } else {
        gfx_gradient_v(x, y, 144, 80, ui_game_color(g, 0), ui_game_color(g, 1));
    }
    gfx_round_rect_outline(x - 1, y - 1, 146, 82, 6, 0xffffff40);

    int ix = x + 160;
    {   // "PSP · ISO · 865 МБ" (1.2.5): kind, file format, size on the card — in a bubble, easy to read
        char kind[96], size[24] = "";
        if (app->card_bytes >= 0) game_size_text(size, sizeof size, app->card_bytes, i18n_get() == LANG_EN);
        const char *fmt = game_format_label(g);
        if (g->format == GF_UMD) snprintf(kind, sizeof kind, "%s", fmt);
        else snprintf(kind, sizeof kind, "%s · %s%s%s", ui_kind_label(g), fmt, size[0] ? " · " : "", size);
        bubble(ix, y, kind);
    }
    // Folders this game belongs to.
    char folders[160] = "";
    for (int f = 0; f < app->lib->folder_count; f++) {
        if (!library_folder_has(app->lib, f, g->id)) continue;
        if (folders[0]) strncat(folders, ", ", sizeof folders - strlen(folders) - 1);
        strncat(folders, app->lib->folders[f].name, sizeof folders - strlen(folders) - 1);
    }
    if (folders[0]) {
        char line[200];
        snprintf(line, sizeof line, "%s %s", tr(S_IN_FOLDERS), folders);
        gfx_text_fit(FONT_SMALL, ix, y + 24, SCREEN_W - ix - 16, COL_MUTED, line);
    }
    if (app->card_dlc) bubble(ix, y + (folders[0] ? 42 : 24), tr(S_CARD_DLC));

    int ty = y + 92;
    gfx_text_fit(FONT_TITLE, x, ty, SCREEN_W - x - 20, COL_TEXT, g->title);
    ty += 30;

    if (g->disc_count > 1) {
        char disc[48];
        snprintf(disc, sizeof disc, tr(S_DISC_N_OF_M), app->nav.card_disc, g->disc_count);
        int dw = gfx_text_width(FONT_BOLD, disc);
        gfx_round_rect(x, ty, dw + 40, 22, 11, COL_PANEL);
        gfx_text(FONT_BOLD, x + 8, ty + 3, app->nav.card_disc > 1 ? COL_ACCENT : 0x46536aff, "‹");
        gfx_text(FONT_BOLD, x + 20, ty + 3, COL_TEXT, disc);
        gfx_text(FONT_BOLD, x + 26 + dw, ty + 3,
                 app->nav.card_disc < g->disc_count ? COL_ACCENT : 0x46536aff, "›");
        ty += 30;
    }
    if (g->kind == GK_UTILITY) {
        gfx_text_fit(FONT_SMALL, x, ty, SCREEN_W - x - 20, COL_WARN, tr(S_UTILITY_WARNING));
        ty += 20;
    }

    const char *play = tr(g->kind == GK_UTILITY ? S_RUN_UTILITY : S_PLAY);
    int pw = gfx_text_width(FONT_BOLD, play) + 40;
    int py = SCREEN_H - HINT_BAR_H - 40;
    gfx_round_rect(x - 3, py - 3, pw + 6, 32, 16, 0x6fb0ff55);
    gfx_round_rect(x, py, pw, 26, 13, g->kind == GK_UTILITY ? COL_WARN : 0xffffffff);
    gfx_button_glyph(input_glyph(ACT_CONFIRM, app->confirm_cross), x + 14, py + 13, 15);
    gfx_text(FONT_BOLD, x + 27, py + 5, 0x111111ff, play);
}
