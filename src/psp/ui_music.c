// Music tab: library (albums | tracks) with a mini player at the bottom (mockup B-1).
#include "ui.h"
#include "art.h"
#include "gfx.h"
#include "player.h"
#include "queue.h"
#include "sjis.h"
#include <stdio.h>
#include <string.h>

#define SIDE_X 8
#define SIDE_W 146
#define ROW_H 17
#define PANEL_X 160
#define MINI_H 24

static const uint32_t COVER_PALETTE[6][2] = {
    { 0x3b1d6eff, 0xe0567aff }, { 0x0f3d4aff, 0x9be3c8ff }, { 0x4a2a0fff, 0xf3c35aff },
    { 0x1d3d7aff, 0x4fb3d9ff }, { 0x5a1d3dff, 0xff7f7fff }, { 0x2a5a2aff, 0xc9d94fff },
};

static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

// First letters of up to three words ("Эхо Большого Взрыва" -> "ЭБВ").
static void initials(const char *title, char *out, size_t cap) {
    size_t o = 0;
    int words = 0, at_start = 1;
    for (const unsigned char *p = (const unsigned char *)title; *p && words < 3; p++) {
        if (*p == ' ' || *p == '-' || *p == '(' || *p == '[') { at_start = 1; continue; }
        if (at_start && (*p & 0xC0) != 0x80) {
            size_t len = *p < 0x80 ? 1 : *p < 0xE0 ? 2 : *p < 0xF0 ? 3 : 4;
            if (o + len >= cap) break;
            memcpy(out + o, p, len);
            o += len;
            words++;
            at_start = 0;
        }
    }
    out[o] = '\0';
}

// Generated cover for albums without artwork: gradient from the title hash + initials.
static void generated_cover(const char *album, int x, int y, int size) {
    uint32_t h = hash_str(album);
    const uint32_t *c = COVER_PALETTE[h % 6];
    gfx_gradient_v(x, y, size, size, c[0], c[1]);
    if (size < 24) return;
    char ini[16];
    initials(album, ini, sizeof ini);
    FontId f = size >= 80 ? FONT_TITLE : FONT_BOLD;
    gfx_text(f, x + size / 10, y + size - gfx_text_height(f) - size / 12, 0xffffffd8, ini);
}

int ui_music_album_of(const MusicLib *l, int track) {
    int lo = 0, hi = l->album_count - 1;
    while (lo <= hi) {                         // albums hold contiguous, ordered track ranges
        int mid = (lo + hi) / 2;
        const Album *a = &l->albums[mid];
        if (track < a->first) hi = mid - 1;
        else if (track >= a->first + a->count) lo = mid + 1;
        else return mid;
    }
    return -1;
}

// Album artwork when available (loaded in the background), otherwise the generated cover.
void ui_music_cover(const App *a, int album, int x, int y, int size) {
    if (album < 0 || album >= a->mlib.album_count) return;
    SDL_Texture *t = art_cover(album);
    if (t) gfx_texture(t, x, y, size, size, 255);
    else generated_cover(a->mlib.albums[album].title, x, y, size);
}

static const char *artist_of(const Track *t) {
    return t->artist[0] ? t->artist : tr(S_MUSIC_UNKNOWN_ARTIST);
}


static void draw_mini_player(const App *a) {
    int y = SCREEN_H - HINT_BAR_H - MINI_H - 2, x = 8, w = SCREEN_W - 16;
    gfx_round_rect(x, y, w, MINI_H, 6, 0x1d2633ff);
    int cur = queue_current(&a->queue);
    if (cur < 0) {
        gfx_text(FONT_SMALL, x + 10, y + 6, COL_MUTED, tr(S_MUSIC_NONE));
        return;
    }
    const Track *t = &a->mlib.tracks[cur];
    ui_music_cover(a, ui_music_album_of(&a->mlib, cur), x + 4, y + 4, 16);
    char line[240];
    snprintf(line, sizeof line, "%s — %s", t->title, artist_of(t));
    gfx_text_fit(FONT_SMALL, x + 26, y + 2, w - 100, COL_TEXT, line);
    uint32_t pos = player_active() ? player_position_ms() : a->music_resume_ms;
    uint32_t dur = player_duration_ms();
    int bar_w = w - 100;
    gfx_round_rect(x + 26, y + 17, bar_w, 3, 1, 0x2b3646ff);
    if (dur > 0) gfx_round_rect(x + 26, y + 17, (int)((uint64_t)bar_w * (pos > dur ? dur : pos) / dur), 3, 1, COL_ACCENT);
    // Transport icons drawn as shapes (the font has no media symbols).
    int ix = x + w - 62, iy = y + MINI_H / 2;
    for (int i = 0; i < 5; i++) gfx_rect(ix + 4 - i, iy - i, 1, i * 2 + 1, COL_MUTED);   // prev
    gfx_rect(ix + 5, iy - 4, 1, 9, COL_MUTED);
    if (music_playing(a)) {
        gfx_rect(ix + 22, iy - 5, 3, 11, COL_TEXT);
        gfx_rect(ix + 28, iy - 5, 3, 11, COL_TEXT);
    } else {
        for (int i = 0; i < 6; i++) gfx_rect(ix + 22 + i, iy - 5 + i, 1, 11 - i * 2, COL_TEXT);
    }
    for (int i = 0; i < 5; i++) gfx_rect(ix + 44 + i, iy - 4 + i, 1, 9 - i * 2, COL_MUTED);   // next
    gfx_rect(ix + 50, iy - 4, 1, 9, COL_MUTED);
}

static float ease(float cur, float target) {
    float d = target - cur;
    return (d > -0.5f && d < 0.5f) ? target : cur + d * 0.3f;
}

// Keep the selected row visible inside a list of `visible` rows.
static int scroll_for(float *scroll, int sel, int visible) {
    float target = *scroll;
    if (sel < target) target = (float)sel;
    if (sel >= target + visible) target = (float)(sel - visible + 1);
    *scroll = ease(*scroll, target);
    return mnav_first_visible(*scroll, sel, visible);
}

static const StrId VIEW_NAMES[4] = { S_MUSIC_ALBUMS, S_MUSIC_ARTISTS, S_MUSIC_FOLDERS, S_MUSIC_ALL_TRACKS };

static int album_of_track(const App *a, int t) {
    for (int i = 0; i < a->mlib.album_count; i++) {
        const Album *al = &a->mlib.albums[i];
        if (t >= al->first && t < al->first + al->count) return i;
    }
    return -1;
}

// Small icons drawn with primitives (the trimmed Inter font has no note/folder glyphs).
static void icon_artist(int x, int y, int s, uint32_t col) {
    gfx_disc(x + s / 2, y + s * 3 / 10, s / 4 > 1 ? s / 4 : 1, col);
    gfx_round_rect(x + s / 5, y + s * 3 / 5, s * 3 / 5, s * 2 / 5, s / 5 > 1 ? s / 5 : 1, col);
}

static void icon_folder(int x, int y, int s, uint32_t col) {
    gfx_round_rect(x, y + s / 6, s * 2 / 5, s / 4, 1, col);
    gfx_round_rect(x, y + s / 3, s, s * 2 / 3, 2, col);
}

static void icon_list(int x, int y, int s, uint32_t col) {
    for (int k = 0; k < 3; k++) gfx_rect(x + 1, y + 2 + k * (s / 3), s - 2, 2, col);
}

static void side_icon(const App *a, SideKind k, int idx, int x, int y) {
    switch (k) {
    case SIDE_ALBUM: ui_music_cover(a, idx, x + 4, y + 2, 11); break;
    case SIDE_ARTIST: icon_artist(x + 4, y + 2, 11, COL_MUTED); break;
    case SIDE_FOLDER: icon_folder(x + 4, y + 2, 11, 0xffcf5aff); break;
    case SIDE_FOLDER_UP: gfx_text(FONT_SMALL, x + 6, y + 1, COL_MUTED, "↑"); break;
    case SIDE_ALL: icon_list(x + 4, y + 2, 11, COL_ACCENT); break;
    default:
        gfx_text(FONT_SMALL, x + 5, y + 1, k == SIDE_FAVORITES ? 0xffcf5aff : COL_MUTED,
                 k == SIDE_FAVORITES ? "★" : k == SIDE_RECENT ? "↻" : "•");
        break;
    }
}

// "MUSIC › Low Roar › EP & Singles" for the current folder (UTF-8 folder names, root first).
static void folder_crumb(const App *a, char *out, size_t cap) {
    int chain[16], depth = 0;
    for (int f = a->mnav.folder; f > 0 && f < a->folders.count && depth < 16; f = a->folders.folders[f].parent)
        chain[depth++] = f;
    size_t n = (size_t)snprintf(out, cap, "MUSIC");
    while (depth-- > 0 && n < cap)
        n += (size_t)snprintf(out + n, cap - n, " › %s", folders_name(&a->folders, chain[depth]));
}

static void draw_library(App *a) {
    const MusicNav *n = &a->mnav;
    int top = TOP_BAR_H + 6, bottom = SCREEN_H - HINT_BAR_H - MINI_H - 6;
    int sides = music_side_count(a);

    // Left column: view name + "□ n/3", (folders: path), then the entries of the view.
    int y = top;
    gfx_text(FONT_SMALL, SIDE_X + 6, y + 2, COL_MUTED, tr(VIEW_NAMES[n->view]));
    {
        char vb[16];
        snprintf(vb, sizeof vb, "%d/4", n->view + 1);
        int bw = 18 + gfx_text_width(FONT_TINY, vb), bx = SIDE_X + SIDE_W - bw - 2;
        gfx_round_rect(bx, y + 1, bw, 13, 6, 0x24324aff);
        gfx_button_glyph(GLYPH_SQUARE, bx + 7, y + 7, 9);
        gfx_text(FONT_TINY, bx + 14, y + 2, 0xcfd8e3ff, vb);
    }
    y += ROW_H;
    if (n->view == 2) {
        char crumb[200];
        folder_crumb(a, crumb, sizeof crumb);
        gfx_text_fit(FONT_SMALL, SIDE_X + 6, y + 2, SIDE_W - 10, COL_TEXT, crumb);
        y += ROW_H;
    }
    int visible = (bottom - y) / ROW_H - (n->view == 2 ? 0 : 1);
    int first = scroll_for(&a->music_scroll[0], n->side_sel, visible);
    for (int i = first; i < sides && y + ROW_H <= bottom; i++) {
        int idx;
        SideKind k = music_side_kind(a, i, &idx);
        if (k == SIDE_FAVORITES) {                    // section header before the playlists
            gfx_text(FONT_SMALL, SIDE_X + 6, y + 2, COL_MUTED, tr(S_MUSIC_PLAYLISTS));
            y += ROW_H;
            if (y + ROW_H > bottom) break;
        }
        int sel = i == n->side_sel;
        if (sel) gfx_round_rect(SIDE_X, y, SIDE_W, ROW_H - 2, 4, n->column == 0 ? 0x2b5fa8ff : 0x24324aff);
        side_icon(a, k, idx, SIDE_X, y);
        gfx_text_fit(FONT_SMALL, SIDE_X + 20, y + 1, SIDE_W - 24, COL_TEXT, music_side_name(a, i));
        y += ROW_H;
    }

    // Right panel: what the selected entry holds.
    int px = PANEL_X, pw = SCREEN_W - PANEL_X - 8;
    gfx_round_rect(px, top - 2, pw, bottom - top + 4, 8, 0x141b26ff);
    if (sides == 0) { draw_mini_player(a); return; }
    int idx;
    SideKind kind = music_side_kind(a, n->side_sel, &idx);
    char info[200], count[32];
    int ty = top + 44, tvis = (bottom - ty) / ROW_H;

    if (kind == SIDE_ARTIST && n->artist_open < 0) {   // artist overview: "All tracks" + albums
        const MArtist *ar = &a->artists.artists[idx];
        gfx_round_rect(px + 6, top + 4, 34, 34, 6, 0x24324aff);
        icon_artist(px + 12, top + 10, 22, COL_ACCENT);
        snprintf(info, sizeof info, tr(S_MUSIC_ARTIST_INFO), ar->album_count, ar->track_count);
        gfx_text_fit(FONT_BOLD, px + 46, top + 4, pw - 52, COL_TEXT, music_side_name(a, n->side_sel));
        gfx_text_fit(FONT_SMALL, px + 46, top + 22, pw - 52, COL_MUTED, info);
        int rows = 1 + ar->album_count;
        int tfirst = scroll_for(&a->music_scroll[1], n->track_sel, tvis);
        for (int r = tfirst; r < rows && r < tfirst + tvis; r++) {
            int ry = ty + (r - tfirst) * ROW_H, sel = n->column == 1 && r == n->track_sel;
            if (sel) gfx_round_rect(px + 4, ry, pw - 8, ROW_H - 2, 4, 0x2b5fa8ff);
            char name[200];
            int cnt;
            if (r == 0) {
                icon_list(px + 10, ry + 2, 11, COL_ACCENT);
                snprintf(name, sizeof name, "%s", tr(S_MUSIC_ALL_TRACKS));
                cnt = ar->track_count;
            } else {
                int al_i = a->artists.albums[ar->album_first + r - 1];
                const Album *al = &a->mlib.albums[al_i];
                ui_music_cover(a, al_i, px + 10, ry + 2, 11);
                if (al->year) snprintf(name, sizeof name, "%s · %d", al->title, al->year);
                else snprintf(name, sizeof name, "%s", al->title);
                cnt = al->count;
            }
            snprintf(count, sizeof count, "%d", cnt);
            int cw = gfx_text_width(FONT_SMALL, count);
            gfx_text_fit(FONT_SMALL, px + 28, ry + 1, pw - 44 - cw, COL_TEXT, name);
            gfx_text(FONT_SMALL, px + pw - 10 - cw, ry + 1, sel ? COL_TEXT : COL_MUTED, count);
        }
        draw_mini_player(a);
        return;
    }

    const int *tracks;
    int count_n = music_side_tracks(a, n->side_sel, &tracks);
    snprintf(count, sizeof count, tr(S_MUSIC_TRACKS_N), count_n);
    const char *title = music_side_name(a, n->side_sel);
    int numbered = kind == SIDE_ALBUM;                  // tag track numbers; folders show file names
    if (kind == SIDE_ALBUM) {
        const Album *al = &a->mlib.albums[idx];
        ui_music_cover(a, idx, px + 6, top + 4, 34);
        const char *artist = al->artist[0] ? al->artist : tr(S_MUSIC_UNKNOWN_ARTIST);
        if (al->year) snprintf(info, sizeof info, "%s · %d · %s", artist, al->year, count);
        else snprintf(info, sizeof info, "%s · %s", artist, count);
    } else if (kind == SIDE_ARTIST) {                   // an opened album or "All tracks" of the artist
        const MArtist *ar = &a->artists.artists[idx];
        const char *who = ar->name[0] ? ar->name : tr(S_MUSIC_UNKNOWN_ARTIST);
        if (n->artist_open == 0) {
            gfx_round_rect(px + 6, top + 4, 34, 34, 6, 0x24324aff);
            icon_list(px + 13, top + 12, 20, COL_ACCENT);
            title = tr(S_MUSIC_ALL_TRACKS);
            snprintf(info, sizeof info, "%s · %s", who, count);
        } else {
            int al_i = a->artists.albums[ar->album_first + n->artist_open - 1];
            const Album *al = &a->mlib.albums[al_i];
            ui_music_cover(a, al_i, px + 6, top + 4, 34);
            title = al->title;
            numbered = 1;
            if (al->year) snprintf(info, sizeof info, "%s · %d · %s", who, al->year, count);
            else snprintf(info, sizeof info, "%s · %s", who, count);
        }
    } else if (kind == SIDE_FOLDER || kind == SIDE_FOLDER_UP) {
        int al_i = count_n > 0 ? album_of_track(a, tracks[0]) : -1;
        if (al_i >= 0) ui_music_cover(a, al_i, px + 6, top + 4, 34);
        else {
            gfx_round_rect(px + 6, top + 4, 34, 34, 6, 0x24324aff);
            icon_folder(px + 12, top + 10, 22, 0xffcf5aff);
        }
        if (count_n > 0) {
            snprintf(info, sizeof info, "%s", count);
        } else {
            int kids[MUSIC_FOLDER_KIDS];
            int f = kind == SIDE_FOLDER ? idx : 0;
            snprintf(info, sizeof info, tr(S_MUSIC_FOLDER_INFO), folders_children(&a->folders, f, kids, MUSIC_FOLDER_KIDS));
        }
    } else if (kind == SIDE_ALL) {
        gfx_round_rect(px + 6, top + 4, 34, 34, 6, 0x24324aff);
        icon_list(px + 13, top + 12, 20, COL_ACCENT);
        snprintf(info, sizeof info, "%s", count);
    } else {
        gfx_round_rect(px + 6, top + 4, 34, 34, 6, 0x24324aff);
        gfx_text(FONT_TITLE, px + 14, top + 9, kind == SIDE_FAVORITES ? 0xffcf5aff : COL_ACCENT,
                 kind == SIDE_FAVORITES ? "★" : kind == SIDE_RECENT ? "↻" : "•");
        snprintf(info, sizeof info, "%s", count);
    }
    gfx_text_fit(FONT_BOLD, px + 46, top + 4, pw - 52, COL_TEXT, title);
    gfx_text_fit(FONT_SMALL, px + 46, top + 22, pw - 52, COL_MUTED, info);
    if (count_n == 0 && kind == SIDE_FOLDER) gfx_text_fit(FONT_SMALL, px + 10, ty + 1, pw - 20, COL_MUTED, tr(S_MUSIC_FOLDER_EMPTY));

    int tfirst = scroll_for(&a->music_scroll[1], n->track_sel, tvis);
    int playing = queue_current(&a->queue);
    for (int i = tfirst; i < count_n && i < tfirst + tvis; i++) {
        const Track *t = &a->mlib.tracks[tracks[i]];
        int ry = ty + (i - tfirst) * ROW_H;
        int sel = n->column == 1 && i == n->track_sel;
        if (sel) gfx_round_rect(px + 4, ry, pw - 8, ROW_H - 2, 4, 0x2b5fa8ff);
        int is_playing = tracks[i] == playing && player_active();
        uint32_t col = is_playing && !sel ? COL_ACCENT : COL_TEXT;
        char no[16];
        snprintf(no, sizeof no, "%d", numbered && t->track_no ? t->track_no : i + 1);
        if (is_playing) gfx_button_glyph(GLYPH_NOTE, px + 14, ry + 7, 10);
        else if (kind != SIDE_ALL) gfx_text(FONT_SMALL, px + 10, ry + 1, COL_MUTED, no);   // 4-digit numbers would overlap
        const char *label = t->title;
        char file[240];
        if (kind == SIDE_ALL && t->artist[0]) {          // the whole library: who sings it
            snprintf(file, sizeof file, "%s · %s", t->title, t->artist);
            label = file;
        }
        if (kind == SIDE_FOLDER) {                      // as on the card: the file name (listed in Shift-JIS)
            const char *slash = strrchr(t->path, '/');
            sjis_to_utf8(slash ? slash + 1 : t->path, file, sizeof file);
            label = file;
        }
        gfx_text_fit(FONT_SMALL, px + 28, ry + 1, pw - 36, col, label);
    }
    draw_mini_player(a);
}

void ui_music_draw(App *a) {
    if (a->media_suspended) {
        const char *t = tr(S_MUSIC_SUSPENDED);
        gfx_text_fit(FONT_BODY, 20, 120, SCREEN_W - 40, COL_MUTED, t);
        return;
    }
    if (a->mlib.track_count == 0 && a->music_lib_loading) {   // 1.3.1: read after start-up
        const char *t = tr(S_MUSIC_LOADING);
        gfx_text(FONT_BODY, (SCREEN_W - gfx_text_width(FONT_BODY, t)) / 2, 110, COL_MUTED, t);
        return;
    }
    if (a->mlib.track_count == 0) {
        const char *t = tr(S_MUSIC_NONE), *h = tr(S_MUSIC_NONE_HINT);
        gfx_text(FONT_TITLE, (SCREEN_W - gfx_text_width(FONT_TITLE, t)) / 2, 74, COL_TEXT, t);
        gfx_text_fit(FONT_SMALL, 20, 106, SCREEN_W - 40, COL_MUTED, h);
        char line[128];
        if (a->music_scan_state == 1) {
            // Progress: animated dots + tracks seen so far.
            snprintf(line, sizeof line, tr(S_MUSIC_SCANNING_N), a->music_scan_seen);
            gfx_text_fit(FONT_BODY, 20, 150, SCREEN_W - 40, COL_ACCENT, line);
            int dots = (int)(a->now_ms / 250) % 8;
            for (int i = 0; i < 8; i++)
                gfx_round_rect(SCREEN_W / 2 - 52 + i * 14, 178, 8, 8, 4, i == dots ? COL_ACCENT : COL_PANEL);
        } else {
            const char *b = tr(S_MUSIC_SCAN_BUTTON);
            int bw = gfx_text_width(FONT_BOLD, b) + 46, bx = (SCREEN_W - bw) / 2;
            gfx_round_rect(bx, 146, bw, 28, 14, 0x2b5fa8ff);
            gfx_button_glyph(input_glyph(ACT_CONFIRM, a->confirm_cross), bx + 18, 160, 14);
            gfx_text(FONT_BOLD, bx + 32, 150, COL_TEXT, b);
            if (a->music_scan_state == 2)
                gfx_text_fit(FONT_SMALL, 20, 186, SCREEN_W - 40, COL_WARN, tr(S_MUSIC_SCAN_NONE));
        }
        return;
    }
    int now = 0;
    for (int i = 0; i < a->mnav.depth; i++) if (a->mnav.stack[i].kind == MS_NOW) now = 1;
    if (now) ui_nowplaying_draw(a);
    else draw_library(a);
    ui_music_overlays_draw(a);
}
