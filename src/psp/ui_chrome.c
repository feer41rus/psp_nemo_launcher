#include "ui.h"
#include "gfx.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

static const StrId TAB_NAMES[TAB_COUNT] = {
    S_TAB_HOME, S_TAB_LIBRARY, S_TAB_MUSIC, S_TAB_NETWORK, S_TAB_SETTINGS,
};

void ui_toast(App *app, const char *msg) {
    snprintf(app->toast, sizeof app->toast, "%s", msg);
    app->toast_until = app->now_ms + 3000;
}

extern const unsigned char ncl_logo[];
extern const unsigned int ncl_logo_size;

// NCL logo on Home, bottom right under the game shelves (Settings › Nemo › NCL logo).
static void draw_ncl_logo(const App *app) {
    static SDL_Texture *tex;
    static int w, h, tried;
    if (!app->lib->settings.ncl_logo || app->nav.tab != TAB_HOME || app->nav.depth) return;
    if (!tried) {
        tried = 1;
        tex = gfx_texture_from_png(ncl_logo, ncl_logo_size);
        if (tex) SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    }
    if (tex) gfx_texture(tex, SCREEN_W - w - 8, SCREEN_H - HINT_BAR_H - h - 4, w, h, 255);   // author: bottom right, under the shelves
}

void ui_chrome_draw(const App *app) {
    draw_ncl_logo(app);
    // Solid bars: artwork never runs under the tabs, clock or battery.
    gfx_rect(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR);

    int x = 8;
    gfx_button_glyph(GLYPH_L, x + 7, TOP_BAR_H / 2, 11);
    x += 20;
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t == TAB_MUSIC && !app->lib->settings.show_music) continue;
        const char *name = tr(TAB_NAMES[t]);
        int active = app->nav.tab == (Tab)t;
        FontId f = active ? FONT_BOLD : FONT_SMALL;
        int w = gfx_text_width(f, name);
        int y = (TOP_BAR_H - gfx_text_height(f)) / 2;
        gfx_text(f, x, y, active ? COL_TEXT : COL_MUTED, name);
        if (active) gfx_rect(x, TOP_BAR_H - 3, w, 2, COL_ACCENT);
        x += w + 11;
    }
    gfx_button_glyph(GLYPH_R, x + 3, TOP_BAR_H / 2, 11);

    Status st;
    platform_status(&st);
    char clock[16];
    DateTime now = { 0, 1, 1, st.hour, st.minute };
    datetime_format_time(&now, app->time_h12, clock, sizeof clock);
    int right = SCREEN_W - 8;
    if (st.battery_pct >= 0) {
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", st.battery_pct);
        int pw = gfx_text_width(FONT_SMALL, pct);
        gfx_text(FONT_SMALL, right - pw, 4, COL_MUTED, pct);
        right -= pw + 3;
        right -= gfx_status_icon(ICON_BATTERY, right - 19, 6, st.battery_pct, st.charging) + 6;
    }
    int cw = gfx_text_width(FONT_BOLD, clock);
    gfx_text(FONT_BOLD, right - cw, 3, COL_TEXT, clock);
    right -= cw + 6;
    if (app->has_wifi) {
        // 0 switch off (dim), 1 on, 3 connected (accent); connecting blinks between 1 and 3.
        int v = st.wifi_on;
        if (st.wifi_on && app->net.phase == NET_CONNECTED) v = 3;
        else if (st.wifi_on && app->net.phase == NET_CONNECTING) v = (app->now_ms / 400) % 2 ? 3 : 1;
        right -= gfx_status_icon(ICON_WIFI, right - 13, 5, v, 0) + 4;
        if (app->ftp.phase != FTP_OFF) {   // FTP pill; dim while it waits for Wi-Fi
            int w = gfx_text_width(FONT_SMALL, "FTP") + 8;
            gfx_round_rect(right - w, 4, w, 12, 4, app->ftp.phase == FTP_ON ? COL_ACCENT : 0x3a4656ff);
            gfx_text(FONT_SMALL, right - w + 4, 3, COL_BAR, "FTP");
            right -= w + 5;
        }
    }
    if (music_playing(app)) right -= gfx_status_icon(ICON_NOTE, right - 12, 6, 0, 0) + 4;
}

void ui_hints_draw(const App *app, const Hint *hints, int n) {
    int y = SCREEN_H - HINT_BAR_H;
    gfx_rect(0, y, SCREEN_W, HINT_BAR_H, COL_BAR);
    int x = 10;
    for (int i = 0; i < n; i++) {
        Glyph g = input_glyph(hints[i].act, app->confirm_cross);
        int gw = g == GLYPH_START ? 33 : g == GLYPH_SELECT ? 40 : g == GLYPH_L || g == GLYPH_R ? 15 : 12;
        gfx_button_glyph(g, x + gw / 2, y + HINT_BAR_H / 2, 12);
        x += gw + 4;
        const char *label = tr(hints[i].label);
        gfx_text(FONT_SMALL, x, y + (HINT_BAR_H - gfx_text_height(FONT_SMALL)) / 2, COL_MUTED, label);
        x += gfx_text_width(FONT_SMALL, label) + 14;
    }
}

void ui_toast_draw(const App *app) {
    if (!app->toast[0] || app->now_ms >= app->toast_until) return;
    int w = gfx_text_width(FONT_BODY, app->toast) + 24;
    if (w > SCREEN_W - 40) w = SCREEN_W - 40;
    int x = (SCREEN_W - w) / 2, y = SCREEN_H - HINT_BAR_H - 34;
    gfx_round_rect(x, y, w, 26, 8, 0x2a3442f0);
    gfx_text_fit(FONT_BODY, x + 12, y + 5, w - 24, COL_TEXT, app->toast);
}

void ui_volume_draw(const App *app) {
    if (app->volume < 0 || app->now_ms >= app->volume_until) return;
    int w = 196, h = 26, x = (SCREEN_W - w) / 2, y = TOP_BAR_H + 8;
    gfx_round_rect(x, y, w, h, 13, 0x1d2633f0);
    // Speaker: box + cone.
    gfx_rect(x + 12, y + 10, 4, 6, COL_TEXT);
    for (int i = 0; i < 5; i++) gfx_rect(x + 16 + i, y + 10 - i, 1, 6 + i * 2, COL_TEXT);
    int bx = x + 30, bw = w - 66;
    gfx_round_rect(bx, y + 11, bw, 5, 2, 0x2b3646ff);
    int fill = app->volume * bw / 30;
    if (fill > 0) gfx_round_rect(bx, y + 11, fill, 5, 2, COL_ACCENT);
    char num[8];
    snprintf(num, sizeof num, "%d", app->volume);
    gfx_text(FONT_BOLD, x + w - 28, y + 5, COL_TEXT, num);
}
