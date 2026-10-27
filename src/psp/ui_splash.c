#include "ui_splash.h"
#include "gfx.h"
#include "i18n.h"
#include "launch.h"
#include "log.h"
#include <stdio.h>
#include <sys/stat.h>

extern const unsigned char splash_c[], splash_ncl[];
extern const unsigned int splash_c_size, splash_ncl_size;

#define COL_LOGO 0x6fb0ffff
#define COL_SLOGAN 0xb8c2cfff
#define BAR_W 260

static SDL_Texture *tex_c, *tex_ncl;

int splash_should_run(const NemoSettings *s) {
    struct stat st;
    int away = stat(LAUNCH_AWAY_FILE, &st) == 0;
    launch_mark_away(0);
    int run = (s->splash_hello || s->splash_ncl) && !away;
    log_msg("splash: hello=%d ncl=%d away=%d run=%d", s->splash_hello, s->splash_ncl, away, run);
    return run;
}

// Text and pictures are drawn at full strength; fading is a black veil over each one, so every frame
// reuses the same cached textures (the text cache is keyed by colour).
static void veil(int x, int y, int w, int h, int alpha) {
    if (alpha < 255) gfx_rect(x, y, w, h, (uint32_t)(255 - alpha));
}

static void centered(FontId f, int y, uint32_t col, const char *s, int alpha) {
    int w = gfx_text_width(f, s), x = (SCREEN_W - w) / 2;
    gfx_text(f, x, y, col, s);
    veil(x - 2, y - 2, w + 4, gfx_text_height(f) + 4, alpha);
}

static void picture(SDL_Texture **t, const unsigned char *png, unsigned size, int y, int alpha, uint32_t tint) {
    if (!*t) *t = gfx_texture_from_png(png, size);
    if (!*t) return;
    int w, h;
    SDL_QueryTexture(*t, NULL, NULL, &w, &h);
    SDL_SetTextureColorMod(*t, (tint >> 24) & 0xff, (tint >> 16) & 0xff, (tint >> 8) & 0xff);
    gfx_texture(*t, (SCREEN_W - w) / 2, y, w, h, (uint8_t)alpha);
}

void splash_draw(const Splash *s, uint32_t now_ms) {
    SplashFx fx;
    splash_fx(s, now_ms, &fx);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    if (s->phase == SPL_HELLO) {
        int ty = 112, sy = ty + gfx_text_height(FONT_TITLE) + 10;
        picture(&tex_c, splash_c, splash_c_size, ty - 56, fx.logo_a, COL_LOGO);
        centered(FONT_TITLE, ty, 0xffffffff, tr(S_SPLASH_HELLO), fx.title_a);
        centered(FONT_BODY, sy, COL_SLOGAN, tr(S_SPLASH_SLOGAN), fx.slogan_a);
        int by = sy + gfx_text_height(FONT_BODY) + 10, half = BAR_W / 2, shown = half * fx.bar_w_pct / 100;
        if (shown > 0) {   // grows from the middle; the whole gradient is drawn, the hidden part covered
            gfx_gradient_h(SCREEN_W / 2 - half, by, half, 2, 0x6fb0ff00, 0x6fb0ffe6);
            gfx_gradient_h(SCREEN_W / 2, by, half, 2, 0x6fb0ffe6, 0x6fb0ff00);
            gfx_rect(SCREEN_W / 2 - half, by, half - shown, 2, 0x000000ff);
            gfx_rect(SCREEN_W / 2 + shown, by, half - shown, 2, 0x000000ff);
            veil(SCREEN_W / 2 - half, by, BAR_W, 2, fx.title_a);
        }
    } else if (s->phase == SPL_NCL) {
        int ty = 104;
        centered(FONT_BODY, ty, COL_SLOGAN, tr(S_SPLASH_THANKS), fx.title_a);
        picture(&tex_ncl, splash_ncl, splash_ncl_size, ty + gfx_text_height(FONT_BODY) + 14, fx.slogan_a, 0xffffffff);
    }
}

void splash_draw_wait(void) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    centered(FONT_SMALL, SCREEN_H / 2 - 6, 0x8c9bb0ff, tr(S_LOADING), 255);
}

void splash_free(void) {
    if (tex_c) SDL_DestroyTexture(tex_c);
    if (tex_ncl) SDL_DestroyTexture(tex_ncl);
    tex_c = tex_ncl = NULL;
}
