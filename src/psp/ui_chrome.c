#include "ui.h"
#include "log.h"
#include "sfx.h"
#include "gfx.h"
#include "platform.h"
#include "tabbar.h"
#include <stdio.h>
#include <string.h>

#define TABS_X 28
#define TABS_AVAIL 300   // up to the R glyph; the clock, battery and Wi-Fi sit right of it
#define TAB_GAP 11
#define TAB_R_SPACE 26   // the R glyph (17 px) and a gap before the status block

static const StrId TAB_NAMES[TAB_COUNT] = {
    S_TAB_HOME, S_TAB_LIBRARY, S_TAB_ORGANIZER, S_TAB_MUSIC, S_TAB_NETWORK, S_TAB_SETTINGS,
};

void ui_toast(App *app, const char *msg) {
    snprintf(app->toast, sizeof app->toast, "%s", msg);
    app->toast_until = app->now_ms + 3000;
    UiSndGate g = sfx_gate(app);
    sfx_play(uisnd_toast(&app->snd, &g, app->now_ms));
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

// The status block at the top right: battery percentage, battery, clock and, on the tab bar, Wi-Fi, FTP and the
// music note. With draw = 0 it only measures; returns the x left of the block. Everything sits on the bar's
// middle line (1.3.9): the digits of semibold 13 px start 4 px below the text top and are 9 px tall, regular
// 11 px — 3 px and 8 px; the Wi-Fi icon spans rows y+1..y+9.
#define BAR_MID (TOP_BAR_H / 2)
static int status_block(const App *app, const Status *st, int full, int draw) {
    char clock[16];
    DateTime now = { 0, 1, 1, st->hour, st->minute };
    datetime_format_time(&now, app->time_h12, clock, sizeof clock);
    int right = SCREEN_W - 8;
    if (st->battery_pct >= 0) {
        if (app->lib->settings.bat_pct) {      // Settings › Nemo › Battery percentage
            char pct[16];
            snprintf(pct, sizeof pct, "%d%%", st->battery_pct);
            int pw = gfx_text_width(FONT_SMALL, pct);
            if (draw) gfx_text(FONT_SMALL, right - pw, BAR_MID - 4 - 3, COL_MUTED, pct);
            right -= pw + 3;
        }
        if (draw) gfx_status_icon(ICON_BATTERY, right - BAT_ICON_W, BAR_MID - 4, st->battery_pct, st->charging);
        right -= BAT_ICON_W + 6;
    }
    int cw = gfx_text_width(FONT_BOLD, clock);
    if (draw) gfx_text(FONT_BOLD, right - cw, BAR_MID - 5 - 4, COL_TEXT, clock);
    right -= cw + 6;
    if (!full) return right;
    if (app->has_wifi) {
        // 0 switch off (dim), 1 on, 3 connected (accent); connecting blinks between 1 and 3.
        int v = st->wifi_on;
        if (st->wifi_on && app->net.phase == NET_CONNECTED) v = 3;
        else if (st->wifi_on && app->net.phase == NET_CONNECTING) v = (app->now_ms / 400) % 2 ? 3 : 1;
        if (draw) gfx_status_icon(ICON_WIFI, right - 13, BAR_MID - 6, v, 0);
        right -= 13 + 4;
        if (app->ftp.phase != FTP_OFF) {   // FTP pill; dim while it waits for Wi-Fi
            int w = gfx_text_width(FONT_SMALL, "FTP") + 8;
            if (draw) {
                gfx_round_rect(right - w, BAR_MID - 6, w, 12, 4, app->ftp.phase == FTP_ON ? COL_ACCENT : 0x3a4656ff);
                gfx_text(FONT_SMALL, right - w + 4, BAR_MID - 4 - 3, COL_BAR, "FTP");   // letters on the % rows
            }
            right -= w + 5;
        }
    }
    if (app->bt.connected == 1 && platform_model() == 4) {   // 1.3.12: only while a device is connected
        if (draw) gfx_status_icon(ICON_BT, right - 7, BAR_MID - 5, 0, 0);
        right -= 7 + 4;
    }
    if (music_playing(app)) {
        if (draw) gfx_status_icon(ICON_NOTE, right - 12, BAR_MID - 4, 0, 0);
        right -= 10 + 4;
    }
    return right;
}

void ui_clock_draw(const App *app) {
    Status st;
    platform_status(&st);
    status_block(app, &st, 0, 1);
}

void ui_chrome_draw(const App *app) {
    draw_ncl_logo(app);
    // Solid bars: artwork never runs under the tabs, clock or battery.
    gfx_rect(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR);
    Status st;
    platform_status(&st);
    int status_left = status_block(app, &st, 1, 0);

    // Tab strip between the L and R glyphs (1.3.1): with more tabs than fit it scrolls smoothly so the
    // open tab is always whole; cut edges fade into the bar. 1.3.11: the strip ends where the status block
    // begins (the music note, FTP), and R slides there smoothly instead of running under the icons.
    int want = status_left - TAB_R_SPACE - TABS_X;
    if (want > TABS_AVAIL) want = TABS_AVAIL;
    static float avail_f = TABS_AVAIL;
    avail_f += ((float)want - avail_f) * 0.3f;
    if (avail_f - (float)want < 0.5f && (float)want - avail_f < 0.5f) avail_f = (float)want;
    int avail = (int)(avail_f + 0.5f);
    int widths[TAB_COUNT], tabs[TAB_COUNT], n = 0, active_i = 0, total = 0;
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t == TAB_MUSIC && !app->lib->settings.show_music) continue;
        if (t == TAB_ORGANIZER && !app->lib->settings.show_photo) continue;
        int active = app->nav.tab == (Tab)t;
        if (active) active_i = n;
        widths[n] = gfx_text_width(active ? FONT_BOLD : FONT_SMALL, tr(TAB_NAMES[t]));
        total += widths[n] + (n ? TAB_GAP : 0);
        tabs[n++] = t;
    }
    static float off;
    static int target;
    target = tabbar_offset(widths, n, active_i, avail, TAB_GAP, target);
    off += ((float)target - off) * 0.3f;
    if (off - (float)target < 0.5f && (float)target - off < 0.5f) off = (float)target;
    int shown = total < avail ? total : avail;
    int x = TABS_X - (int)off;
    for (int i = 0; i < n; i++) {
        int active = i == active_i;
        FontId f = active ? FONT_BOLD : FONT_SMALL;
        int y = (TOP_BAR_H - gfx_text_height(f)) / 2;
        gfx_text(f, x, y, active ? COL_TEXT : COL_MUTED, tr(TAB_NAMES[tabs[i]]));
        if (active) gfx_rect(x, TOP_BAR_H - 3, widths[i], 2, COL_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    // No GPU clipping (the PSP scissor is a trap): the bar is painted again outside the strip.
    gfx_rect(0, 0, TABS_X, TOP_BAR_H, COL_BAR);
    gfx_rect(TABS_X + shown, 0, SCREEN_W - TABS_X - shown, TOP_BAR_H, COL_BAR);
    gfx_button_glyph(GLYPH_L, 15, TOP_BAR_H / 2, 11);
    if (off > 0.5f) gfx_gradient_h(TABS_X, 0, 14, TOP_BAR_H, COL_BAR, COL_BAR & 0xffffff00u);
    if ((int)off + avail < total) gfx_gradient_h(TABS_X + shown - 14, 0, 14, TOP_BAR_H, COL_BAR & 0xffffff00u, COL_BAR);
    gfx_button_glyph(GLYPH_R, TABS_X + shown + 9, TOP_BAR_H / 2, 11);
    status_block(app, &st, 1, 1);
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

void ui_fail(App *app, StrId msg) {
    log_fail("%s (on %s)", tr_en(msg), perflog_screen());
    ui_toast(app, tr(msg));
}
