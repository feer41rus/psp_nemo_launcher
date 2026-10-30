// Video (1.3.3): a window of the Organizer (or opened from the file manager) — the videos of ms0:/VIDEO,
// a full-screen player, "Continue from 12:34 or start over?", subtitles (track, size, background, height,
// time shift with SELECT+Left/Right). Music pauses while the window is open and goes on after it.
#include "ui.h"
#include "art.h"
#include "fsbrowse.h"
#include "gamesize.h"
#include "gfx.h"
#include "log.h"
#include "player.h"
#include "platform.h"
#include "safefile.h"
#include "subs.h"
#include "utf8.h"
#include "vformat.h"
#include "video_hw.h"
#include "vresume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define VIDEO_DIR "ms0:/VIDEO"
#define RESUME_FILE "ms0:/PSP/NEMO/video_resume.json"
#define MAX_ITEMS 500
#define ROW_H 18
#define LIST_ROWS 12
#define SUB_TRACKS 8
#define HUD_MS 3000
#define SAVE_EVERY_MS 30000
#define SEEK_SHORT 10000
#define SEEK_LONG 60000
#define SUB_MAX_W 440

static const int SUB_PX[5] = { 12, 14, 16, 19, 23 };

typedef enum { VW_LIST, VW_PLAYER } VView;
typedef enum { VM_NONE, VM_RESUME_Q, VM_MENU, VM_INFO, VM_ERROR } VModal;
typedef enum { MI_INFO, MI_TITLE, MI_AUDIO, MI_SUBS, MI_SIZE, MI_BG, MI_POS, MI_COUNT } VMenu;   // TITLE/AUDIO: UMD only (1.3.4)

typedef struct { char path[FS_PATH_MAX]; char shown[256]; long long size; } VidItem;

typedef struct VideoWin {
    VView view;
    VModal modal;
    int from_files;                // opened on one file from the file manager: ○ goes back there
    char last_path[FS_PATH_MAX];
    VidItem *items;
    int n, sel, top;
    // player
    char path[FS_PATH_MAX];
    VideoInfo info;
    char key[VR_KEY_MAX];
    int64_t resume_ms;
    SDL_Texture *tex;
    int tex_w, tex_h, have_frame;
    uint32_t input_ms, saved_ms;
    int music_was;
    int started;                   // the video played (not just the "Continue?" question): its place may be stored
    int pending_track;             // UMD: the stored sound track, set once playing (-1 none)
    int menu_sel, yes;
    VMenu mitems[MI_COUNT];        // the rows of this menu
    int n_mitems;
    VideoStream streams[16];       // UMD Video: the disc's streams, read on the first menu (-1 = not yet)
    int n_streams, stream_sel;
    // subtitles
    SubList subs[SUB_TRACKS];
    char sub_names[SUB_TRACKS][256];
    int n_subs, track;
    int32_t sub_offset;
    char sub_text[512];
    int sub_lines, sub_style;      // what the textures were made for (size)
    SDL_Texture *sub_tex[SUBS_MAX_LINES], *sub_sh[SUBS_MAX_LINES];
    int sub_w[SUBS_MAX_LINES], sub_h[SUBS_MAX_LINES];
    char note[320];
    // "Sorry, this format is not supported": the format and codec line, and why
    char err_line[160], err_detail[200];
    uint32_t note_until;
} VideoWin;

static int is_umd(const VideoWin *s) { return !strncmp(s->path, "umd:", 4); }

static VResume vres;
static int vres_loaded;

static void resume_load(void) {
    if (vres_loaded) return;
    vresume_init(&vres);
    int missing = 0;
    char *t = safe_read_text(RESUME_FILE, 256 * 1024, &missing);
    vresume_load(&vres, t);
    free(t);
    vres_loaded = 1;
}

static void resume_save(void) {
    char *t = vresume_save(&vres);
    if (t) {
        int rc = safe_write_text(RESUME_FILE, t);
        if (rc != 0) log_msg("video: resume save rc=%d", rc);
    }
    free(t);
}

static void fmt_time(int64_t ms, char *out, size_t cap) {
    int s = (int)(ms / 1000);
    if (s >= 3600) snprintf(out, cap, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, cap, "%d:%02d", s / 60, s % 60);
}

static int is_video(const char *name) { return vformat_is_video(name); }

int video_is_file(const char *name) { return is_video(name); }

// ---------- the list ----------

static void add_dir(VideoWin *s, const char *dir, int depth) {
    FsList l;
    if (fs_list(dir, &l) != 0) return;
    for (int i = 0; i < l.count && s->n < MAX_ITEMS; i++) {
        char p[FS_PATH_MAX];
        if (fs_join(p, sizeof p, dir, l.items[i].name) != 0) continue;
        if (l.items[i].is_dir) { if (depth < 1) add_dir(s, p, depth + 1); continue; }
        if (!is_video(l.items[i].name)) continue;
        VidItem *it = &s->items[s->n++];
        snprintf(it->path, sizeof it->path, "%s", p);
        fs_display(l.items[i].name, it->shown, sizeof it->shown);
        it->size = l.items[i].size;
    }
    fs_list_free(&l);
}

static int by_shown(const void *a, const void *b) { return strcasecmp(((const VidItem *)a)->shown, ((const VidItem *)b)->shown); }

static App *scan_app;   // for the UMD Video row

static void scan(VideoWin *s) {
    uint32_t t0 = platform_ms();
    s->n = 0;
    add_dir(s, VIDEO_DIR, 0);
    qsort(s->items, (size_t)s->n, sizeof *s->items, by_shown);
    if (scan_app && scan_app->umd_video && s->n < MAX_ITEMS) {   // the disc first (1.3.3)
        memmove(s->items + 1, s->items, (size_t)s->n * sizeof *s->items);
        VidItem *it = &s->items[0];
        snprintf(it->path, sizeof it->path, "umd:%s", scan_app->umd_video_id);
        snprintf(it->shown, sizeof it->shown, tr(S_VID_UMD_ROW_FMT), scan_app->umd_video_title);
        it->size = 0;
        s->n++;
    }
    log_time("video_list", platform_ms() - t0);
}

// ---------- subtitles ----------

static void subs_drop_textures(VideoWin *s) {
    for (int i = 0; i < SUBS_MAX_LINES; i++) {
        if (s->sub_tex[i]) SDL_DestroyTexture(s->sub_tex[i]);
        if (s->sub_sh[i]) SDL_DestroyTexture(s->sub_sh[i]);
        s->sub_tex[i] = s->sub_sh[i] = NULL;
    }
    s->sub_lines = 0;
    s->sub_text[0] = '\0';
}

static void subs_close(VideoWin *s) {
    subs_drop_textures(s);
    for (int i = 0; i < s->n_subs; i++) subs_free(&s->subs[i]);
    s->n_subs = 0;
    s->track = -1;
}

static void subs_load(VideoWin *s) {
    char dir[FS_PATH_MAX], names[SUB_TRACKS][256];
    snprintf(dir, sizeof dir, "%s", s->path);
    fs_parent(dir);
    int n = subs_find(dir, fs_name(s->path), names, SUB_TRACKS);
    for (int i = 0; i < n; i++) {
        char p[FS_PATH_MAX];
        if (fs_join(p, sizeof p, dir, names[i]) != 0) continue;
        int missing = 0;
        char *t = safe_read_text(p, 2 * 1024 * 1024, &missing);
        if (!t) continue;
        SubList *l = &s->subs[s->n_subs];
        memset(l, 0, sizeof *l);
        if (subs_parse(l, t, strlen(t)) > 0) {
            fs_display(names[i], s->sub_names[s->n_subs], sizeof s->sub_names[0]);
            s->n_subs++;
        }
        free(t);
    }
    if (s->n_subs < SUB_TRACKS) {   // MP4: subtitles inside the file come after the files beside it
        SubList *l = &s->subs[s->n_subs];
        memset(l, 0, sizeof *l);
        if (video_text_track(l) > 0) {
            snprintf(s->sub_names[s->n_subs], sizeof s->sub_names[0], tr(S_VID_EMBEDDED_FMT), video_text_lang());
            s->n_subs++;
        }
    }
    log_msg("video: %d subtitle track(s)", s->n_subs);
}

static int sub_width(void *ctx, const char *t) { (void)ctx; return gfx_text_width(FONT_SUB, t); }

static void note(VideoWin *s, const App *a, const char *text) {
    snprintf(s->note, sizeof s->note, "%s", text);
    s->note_until = a->now_ms + 2000;
}

// ---------- the player ----------

static void store_place(VideoWin *s) {
    if (!s->key[0] || !s->started) return;   // review I1: backing out of "Continue?" keeps the saved place
    vresume_put(&vres, s->key, video_pos_ms(), s->info.duration_ms, s->track, s->sub_offset);
    resume_save();
}

static void start_playing(App *a, VideoWin *s, int64_t from) {
    s->modal = VM_NONE;
    s->input_ms = a->now_ms;
    s->saved_ms = a->now_ms;
    s->started = 1;
    video_play(from);
    if (s->pending_track >= 0) { video_set_audio_track(s->pending_track); s->pending_track = -1; }
    log_msg("video: play from %lld ms", (long long)from);
    if (s->track >= 0 && s->track < s->n_subs) {
        char m[300];
        snprintf(m, sizeof m, tr(S_VID_SUB_ON_FMT), s->sub_names[s->track]);
        note(s, a, m);
    }
}

static void show_error(VideoWin *s, const char *detail) {
    snprintf(s->err_line, sizeof s->err_line, tr(S_VID_FORMAT_FMT), s->info.container[0] ? s->info.container : "?",
             vformat_codec_name(s->info.vcodec));
    snprintf(s->err_detail, sizeof s->err_detail, "%s", detail ? detail : "");
    s->modal = VM_ERROR;
    s->view = VW_LIST;
}

static void open_player(App *a, VideoWin *s, const char *path) {
    char why[160];
    memset(&s->info, 0, sizeof s->info);
    VideoOpen r = video_open(path, &s->info, why, sizeof why);
    if (r != VO_OK) {
        log_msg("video: %s not played (%d): %s %s %s", path, r, s->info.container, s->info.vcodec, why);
        show_error(s, why);
        return;
    }
    snprintf(s->path, sizeof s->path, "%s", path);
    s->view = VW_PLAYER;
    s->started = 0;
    s->pending_track = -1;
    s->have_frame = 0;
    if (s->tex && (s->tex_w != s->info.width || s->tex_h != s->info.height)) { SDL_DestroyTexture(s->tex); s->tex = NULL; }
    if (!s->tex) {
        s->tex = SDL_CreateTexture(gfx_renderer(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, s->info.width, s->info.height);
        if (s->tex) SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_NONE);
        s->tex_w = s->info.width;
        s->tex_h = s->info.height;
    }
    subs_close(s);
    subs_load(s);
    vresume_key(s->key, sizeof s->key, path, strncmp(path, "umd:", 4) ? s->info.size : 0);   // a disc: by its ID
    const VrEntry *e = vresume_get(&vres, s->key);
    s->track = e ? e->sub_track : (s->n_subs ? 0 : -1);
    if (s->track >= s->n_subs) s->track = s->n_subs ? 0 : -1;
    s->sub_offset = e ? e->sub_offset : 0;
    s->n_streams = -1;   // a UMD's streams are read on the first menu
    if (is_umd(s) && e && e->audio_track) {   // the sound track chosen last time (1.3.4)
        int cur, mask = video_audio_tracks(&cur);
        if (mask & (1 << e->audio_track)) s->pending_track = e->audio_track;
    }
    if (vresume_ask(e)) {
        s->resume_ms = e->pos_ms;
        s->modal = VM_RESUME_Q;
        s->yes = 1;
        return;
    }
    start_playing(a, s, 0);
}

static void close_player(App *a, VideoWin *s) {
    store_place(s);
    int d, dr, wo;
    video_stats(&d, &dr, &wo);
    log_msg("video: stop at %lld ms; %d pictures, %d skipped, worst %d ms, sound buffers late %d", (long long)video_pos_ms(), d, dr,
            wo, video_sound_late());
    snprintf(s->last_path, sizeof s->last_path, "%s", s->path);
    video_close();
    subs_close(s);
    s->key[0] = '\0';
    s->view = VW_LIST;
    s->modal = VM_NONE;
    (void)a;
}

// ---------- the window ----------

static VideoWin *win(const App *a) { return a->video; }

int video_win_is_open(const App *a) { return a->video != NULL; }
int video_win_playing(const App *a) { return a->video && a->video->view == VW_PLAYER && !video_paused(); }
const char *video_last_path(const App *a) { return a->video ? a->video->last_path : ""; }

const char *video_screen(const App *a) {
    const VideoWin *s = win(a);
    if (!s) return "";
    return s->view == VW_PLAYER ? (s->modal == VM_MENU ? "player+menu" : "player") : "list";
}

static VideoWin *open_window(App *a) {
    if (a->video) return a->video;
    art_reset();
    VideoWin *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->items = calloc(MAX_ITEMS, sizeof *s->items);
    if (!s->items) { free(s); return NULL; }
    s->track = -1;
    a->video = s;
    resume_load();
    if (player_active() && music_current_is_umd(a)) {   // both on sceMpeg: the UMD Music track gives way (1.3.5)
        log_msg("video: UMD Music track stopped for the video");
        player_stop();
    }
    s->music_was = player_active() && !player_paused();
    if (s->music_was) player_pause(1);   // one sound at a time
    log_msg("video: window open");
    return s;
}

void video_win_open(App *a) {
    VideoWin *s = open_window(a);
    scan_app = a;
    if (s) scan(s);
}

void video_open_umd(App *a) {
    VideoWin *s = open_window(a);
    if (!s) return;
    if (s->view == VW_PLAYER) close_player(a, s);   // another video was playing: its place first (review I4)
    scan_app = a;
    scan(s);
    char path[40];
    snprintf(path, sizeof path, "umd:%s", a->umd_video_id);
    open_player(a, s, path);
}

void video_open_file(App *a, const char *path) {
    VideoWin *s = open_window(a);
    if (!s) return;
    s->from_files = 1;
    open_player(a, s, path);
}

static void close_window(App *a) {
    VideoWin *s = a->video;
    if (!s) return;
    if (s->view == VW_PLAYER) close_player(a, s);
    if (s->tex) SDL_DestroyTexture(s->tex);
    if (s->music_was) player_pause(0);
    char last[FS_PATH_MAX];
    snprintf(last, sizeof last, "%s", s->last_path);
    free(s->items);
    free(s);
    a->video = NULL;
    platform_log_memory("video-closed");
    (void)last;
}

void video_sub_shift(App *a, int delta_ms) {
    VideoWin *s = win(a);
    if (!s || s->view != VW_PLAYER || s->track < 0) return;
    s->sub_offset += delta_ms;
    if (s->sub_offset > 60000) s->sub_offset = 60000;
    if (s->sub_offset < -60000) s->sub_offset = -60000;
    char v[16], m[64];
    snprintf(v, sizeof v, "%+d,%d", (int)(s->sub_offset / 1000), (int)(abs((int)s->sub_offset) % 1000 / 100));
    if (s->sub_offset < 0 && s->sub_offset > -1000) snprintf(v, sizeof v, "-0,%d", (int)(abs((int)s->sub_offset) / 100));
    snprintf(m, sizeof m, tr(S_VID_SUB_SHIFT_FMT), v);
    note(s, a, m);
}

// "umd:<ID>" or "umd:<ID>/<FILE>": the disc ID part.
static void umd_id(const VideoWin *s, char *out, size_t cap) {
    const char *p = s->path + 4, *slash = strchr(p, '/');
    snprintf(out, cap, "%.*s", slash ? (int)(slash - p) : (int)strlen(p), p);
}

static void open_menu(VideoWin *s) {
    s->n_mitems = 0;
    s->mitems[s->n_mitems++] = MI_INFO;
    if (is_umd(s)) {
        if (s->n_streams < 0) {   // read once: the disc is slow
            s->n_streams = video_umd_streams(s->streams, 16);
            s->stream_sel = 0;
            const char *cur = strchr(s->path + 4, '/');
            long long best = -1;
            for (int i = 0; i < s->n_streams; i++) {
                if (cur ? !strcasecmp(cur + 1, s->streams[i].file) : s->streams[i].size > best) { s->stream_sel = i; best = s->streams[i].size; }
                if (cur && !strcasecmp(cur + 1, s->streams[i].file)) break;
            }
        }
        if (s->n_streams > 1) s->mitems[s->n_mitems++] = MI_TITLE;
        int cur_track, mask = video_audio_tracks(&cur_track);
        if (mask & (mask - 1)) s->mitems[s->n_mitems++] = MI_AUDIO;   // more than one track
    }
    s->mitems[s->n_mitems++] = MI_SUBS;
    s->mitems[s->n_mitems++] = MI_SIZE;
    s->mitems[s->n_mitems++] = MI_BG;
    s->mitems[s->n_mitems++] = MI_POS;
    s->menu_sel = 0;
    s->modal = VM_MENU;
}

static void open_player(App *a, VideoWin *s, const char *path);
static void close_player(App *a, VideoWin *s);

static void menu_change(App *a, VideoWin *s, int dir) {
    switch (s->mitems[s->menu_sel]) {
    case MI_TITLE:
        if (s->n_streams > 0) s->stream_sel = (s->stream_sel + s->n_streams + dir) % s->n_streams;
        break;
    case MI_AUDIO: {   // the next track found, at once
        int cur, mask = video_audio_tracks(&cur);
        for (int k = 1; k <= 16; k++) {
            int ch = ((cur + dir * k) % 16 + 16) % 16;
            if (mask & (1 << ch)) {
                video_set_audio_track(ch);
                vresume_set_audio(&vres, s->key, ch);
                resume_save();
                break;
            }
        }
        break;
    }
    case MI_SUBS:
        if (!s->n_subs) break;
        s->track += dir;
        if (s->track >= s->n_subs) s->track = -1;
        if (s->track < -1) s->track = s->n_subs - 1;
        subs_drop_textures(s);
        break;
    case MI_SIZE: vres.sub_size = (vres.sub_size + 5 + dir) % 5; subs_drop_textures(s); break;
    case MI_BG: vres.sub_bg = (vres.sub_bg + 4 + dir) % 4; break;
    case MI_POS: vres.sub_pos = (vres.sub_pos + 9 + dir) % 9; break;
    default: break;
    }
    (void)a;
}

static void list_action(App *a, VideoWin *s, Action act) {
    switch (act) {
    case ACT_UP: if (s->n) s->sel = (s->sel + s->n - 1) % s->n; break;
    case ACT_DOWN: if (s->n) s->sel = (s->sel + 1) % s->n; break;
    case ACT_TAB_PREV: s->sel = s->sel > LIST_ROWS ? s->sel - LIST_ROWS : 0; break;
    case ACT_TAB_NEXT: s->sel = s->sel + LIST_ROWS < s->n ? s->sel + LIST_ROWS : (s->n ? s->n - 1 : 0); break;
    case ACT_CONFIRM: if (s->n) open_player(a, s, s->items[s->sel].path); break;
    case ACT_BACK: close_window(a); return;
    default: break;
    }
    if (s->sel < s->top) s->top = s->sel;
    if (s->sel >= s->top + LIST_ROWS) s->top = s->sel - LIST_ROWS + 1;
}

static void player_back(App *a, VideoWin *s) {
    close_player(a, s);
    if (s->from_files) close_window(a);
    else scan(s);   // the list shows where it stopped
}

static void player_action(App *a, VideoWin *s, Action act) {
    s->input_ms = a->now_ms;
    if (s->modal == VM_RESUME_Q) {
        if (act == ACT_LEFT || act == ACT_RIGHT) s->yes = !s->yes;
        else if (act == ACT_CONFIRM) start_playing(a, s, s->yes ? s->resume_ms : 0);
        else if (act == ACT_BACK) player_back(a, s);
        return;
    }
    if (s->modal == VM_INFO) {
        if (act == ACT_BACK || act == ACT_CONFIRM || act == ACT_MENU) s->modal = VM_MENU;
        return;
    }
    if (s->modal == VM_MENU) {
        if (act == ACT_UP) s->menu_sel = (s->menu_sel + s->n_mitems - 1) % s->n_mitems;
        else if (act == ACT_DOWN) s->menu_sel = (s->menu_sel + 1) % s->n_mitems;
        else if (act == ACT_LEFT) menu_change(a, s, -1);
        else if (act == ACT_RIGHT) menu_change(a, s, 1);
        else if (act == ACT_CONFIRM) {
            VMenu m = s->mitems[s->menu_sel];
            if (m == MI_INFO) s->modal = VM_INFO;
            else if (m == MI_TITLE && s->n_streams > 0) {   // play the stream chosen (the current one's place kept)
                char id[32], path[96];
                umd_id(s, id, sizeof id);
                snprintf(path, sizeof path, "umd:%s/%s", id, s->streams[s->stream_sel].file);
                log_msg("video: umd stream chosen: %s", path);
                close_player(a, s);
                open_player(a, s, path);
            } else menu_change(a, s, 1);
        }
        else if (act == ACT_BACK || act == ACT_MENU) { s->modal = VM_NONE; store_place(s); }
        return;
    }
    switch (act) {
    case ACT_CONFIRM:
        video_pause(!video_paused());
        if (video_paused()) store_place(s);
        break;
    case ACT_LEFT: video_seek(video_pos_ms() - SEEK_SHORT); break;
    case ACT_RIGHT: video_seek(video_pos_ms() + SEEK_SHORT); break;
    case ACT_TAB_PREV: video_seek(video_pos_ms() - SEEK_LONG); break;
    case ACT_TAB_NEXT: video_seek(video_pos_ms() + SEEK_LONG); break;
    case ACT_MENU: open_menu(s); break;
    case ACT_BACK:
    case ACT_HOME: player_back(a, s); break;
    default: break;
    }
}

void video_win_action(App *a, Action act) {
    VideoWin *s = win(a);
    if (!s) return;
    if (s->modal == VM_ERROR) {
        if (act == ACT_BACK || act == ACT_CONFIRM || act == ACT_HOME) {
            s->modal = VM_NONE;
        }
        return;
    }
    if (s->view == VW_PLAYER) player_action(a, s, act);
    else if (act != ACT_HOME) list_action(a, s, act);
}

void video_win_tick(App *a) {
    VideoWin *s = win(a);
    if (!s) return;
    if (a->nav.tab != TAB_ORGANIZER && !(s->from_files && files_is_open(a))) { close_window(a); return; }
    if (s->from_files && s->view == VW_LIST && s->modal == VM_NONE) { close_window(a); return; }   // the file did not open
    if (s->view != VW_PLAYER || s->modal == VM_RESUME_Q) return;
    const uint32_t *px;
    int w, h, stride;
    if (video_take_frame(&px, &w, &h, &stride)) {
        if (s->tex) {
            SDL_Rect r = { 0, 0, w, h };
            SDL_UpdateTexture(s->tex, &r, px, stride * 4);
        }
        video_frame_done();
        s->have_frame = 1;
    }
    if (!video_paused() && a->now_ms - s->saved_ms > SAVE_EVERY_MS) { s->saved_ms = a->now_ms; store_place(s); }
    static uint32_t dbg_ms;   // detailed log: the player's state every second
    if (log_verbose && a->now_ms - dbg_ms >= 1000) {
        dbg_ms = a->now_ms;
        int d, dr, wo;
        video_stats(&d, &dr, &wo);
        log_dbg("video: at %lld ms, pictures %d, skipped %d, worst %d ms, paused %d, subtitle offset %d", (long long)video_pos_ms(), d,
                dr, wo, video_paused(), (int)s->sub_offset);
    }
    if (video_ended()) { log_msg("video: ended"); player_back(a, s); }
}

// ---------- drawing ----------

// The name to show: the file's, or the UMD Video disc's title.
static void title_of(const VideoWin *s, char *out, size_t cap) {
    if (!strncmp(s->path, "umd:", 4) && scan_app) snprintf(out, cap, "%s", scan_app->umd_video_title);
    else fs_display(fs_name(s->path), out, cap);
}

static void draw_box(int w, int h, int *x, int *y) {
    *x = (SCREEN_W - w) / 2;
    *y = (SCREEN_H - h) / 2;
    gfx_round_rect(*x, *y, w, h, 12, 0x1a2230f0);
    gfx_round_rect_outline(*x, *y, w, h, 12, 0xffffff20);
}

static void draw_subs(VideoWin *s, int hud) {
    if (s->track < 0 || s->track >= s->n_subs) return;
    char text[512];
    subs_at(&s->subs[s->track], video_pos_ms(), s->sub_offset, text, sizeof text);
    if (strcmp(text, s->sub_text) != 0 || s->sub_style != vres.sub_size || (!s->sub_lines && text[0])) {
        subs_drop_textures(s);
        snprintf(s->sub_text, sizeof s->sub_text, "%s", text);
        s->sub_style = vres.sub_size;
        gfx_sub_font_size(SUB_PX[vres.sub_size]);
        char lines[SUBS_MAX_LINES][256];
        s->sub_lines = text[0] ? subs_wrap(text, SUB_MAX_W, sub_width, NULL, lines) : 0;
        for (int i = 0; i < s->sub_lines; i++) {
            s->sub_tex[i] = gfx_text_texture(FONT_SUB, 0xffffffff, lines[i], &s->sub_w[i], &s->sub_h[i]);
            int w2, h2;
            s->sub_sh[i] = gfx_text_texture(FONT_SUB, 0x000000ff, lines[i], &w2, &h2);
        }
    }
    int lh = SUB_PX[vres.sub_size] + 6;
    int bottom = SCREEN_H - 8 - vres.sub_pos * 14 - (hud ? 30 : 0);
    for (int i = 0; i < s->sub_lines; i++) {
        if (!s->sub_tex[i]) continue;
        int w = s->sub_w[i], h = s->sub_h[i];
        int x = (SCREEN_W - w) / 2, y = bottom - (s->sub_lines - i) * lh;
        if (vres.sub_bg == 2) gfx_rect(x - 6, y - 1, w + 12, lh, 0x00000099);
        if (vres.sub_bg == 3) gfx_rect(x - 6, y - 1, w + 12, lh, 0x000000ff);
        if (vres.sub_bg == 1 && s->sub_sh[i]) {   // a shadow all round: readable on light pictures too
            static const int D[4][2] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
            for (int k = 0; k < 4; k++) {
                SDL_Rect d = { x + D[k][0], y + D[k][1], w, h };
                SDL_RenderCopy(gfx_renderer(), s->sub_sh[i], NULL, &d);
            }
        }
        SDL_Rect d = { x, y, w, h };
        SDL_RenderCopy(gfx_renderer(), s->sub_tex[i], NULL, &d);
    }
}

static const char *menu_value(const VideoWin *s, VMenu m, char *buf, size_t cap) {
    if (m == MI_TITLE && s->n_streams > 0) {
        const VideoStream *v = &s->streams[s->stream_sel];
        char t[24];
        fmt_time(v->duration_ms > 0 ? v->duration_ms : 0, t, sizeof t);
        long long best = 0;
        for (int i = 0; i < s->n_streams; i++) if (s->streams[i].size > best) best = s->streams[i].size;
        if (v->size == best) snprintf(buf, cap, tr(S_VID_MAIN_FMT), t);
        else snprintf(buf, cap, tr(S_VID_CLIP_FMT), s->stream_sel + 1, t);
        return buf;
    }
    if (m == MI_AUDIO) {
        int cur;
        video_audio_tracks(&cur);
        snprintf(buf, cap, tr(S_VID_TRACK_FMT), cur + 1);
        return buf;
    }
    static const StrId BG[4] = { S_VID_BG_NONE, S_VID_BG_SHADOW, S_VID_BG_HALF, S_VID_BG_BLACK };
    switch (m) {
    case MI_SUBS: return !s->n_subs ? tr(S_VID_SUBS_NONE) : s->track < 0 ? tr(S_VID_SUBS_OFF) : s->sub_names[s->track];
    case MI_SIZE: snprintf(buf, cap, "%d", vres.sub_size + 1); return buf;
    case MI_BG: return tr(BG[vres.sub_bg]);
    case MI_POS: snprintf(buf, cap, "%d", vres.sub_pos); return buf;
    default: return "";
    }
}

static void draw_menu(const VideoWin *s) {
    static const StrId L[MI_COUNT] = { S_VID_INFO, S_VID_TITLE, S_VID_AUDIO, S_VID_SUBS, S_VID_SUB_SIZE, S_VID_SUB_BG, S_VID_SUB_POS };
    int w = 340, h = 16 + s->n_mitems * 24, x, y;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < s->n_mitems; i++) {
        int ry = y + 8 + i * 24;
        if (i == s->menu_sel) gfx_round_rect(x + 6, ry, w - 12, 22, 6, 0x24324aff);
        gfx_text(FONT_BODY, x + 16, ry + 2, COL_TEXT, tr(L[s->mitems[i]]));
        char b[16];
        const char *v = menu_value(s, s->mitems[i], b, sizeof b);
        int vw = gfx_text_width(FONT_BODY, v);
        if (vw > 150) vw = 150;
        gfx_text_fit(FONT_BODY, x + w - 16 - vw, ry + 2, 150, i == s->menu_sel ? COL_ACCENT : COL_MUTED, v);
    }
}

static void draw_info(const VideoWin *s) {
    char fmt[48], res[32], fps[24], dur[24], size[32];
    snprintf(fmt, sizeof fmt, "%s · %s", s->info.container, s->info.vcodec);
    snprintf(res, sizeof res, "%d × %d", s->info.width, s->info.height);
    snprintf(fps, sizeof fps, "%d,%02d", s->info.fps_x100 / 100, s->info.fps_x100 % 100);
    fmt_time(s->info.duration_ms, dur, sizeof dur);
    game_size_text(size, sizeof size, s->info.size, i18n_get() == LANG_EN);
    char name[256];
    title_of(s, name, sizeof name);
    const StrId L[7] = { S_PH_I_NAME, S_VID_I_FORMAT, S_VID_I_RES, S_VID_I_FPS, S_VID_I_AUDIO, S_VID_I_DUR, S_VID_I_SIZE };
    const char *V[7] = { name, fmt, res, fps, s->info.audio[0] ? s->info.audio : "—", dur, size };
    int x, y, w = 420, h = 20 + 7 * 18;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < 7; i++) {
        gfx_text(FONT_SMALL, x + 14, y + 10 + i * 18, COL_MUTED, tr(L[i]));
        gfx_text_fit(FONT_SMALL, x + 130, y + 10 + i * 18, w - 144, COL_TEXT, V[i]);
    }
}

static void draw_resume_q(const VideoWin *s) {
    char t[24], q[96];
    fmt_time(s->resume_ms, t, sizeof t);
    snprintf(q, sizeof q, tr(S_VID_RESUME_Q_FMT), t);
    int x, y, w = 360, h = 96;
    draw_box(w, h, &x, &y);
    gfx_text_fit(FONT_BODY, x + 16, y + 18, w - 32, COL_TEXT, q);
    const StrId L[2] = { S_VID_CONTINUE, S_VID_FROM_START };
    for (int i = 0; i < 2; i++) {
        int on = (i == 0) == (s->yes != 0);
        int bx = x + w / 2 - 128 + i * 136, by = y + h - 36;
        gfx_round_rect(bx, by, 120, 24, 12, on ? 0x2b5fa8ff : COL_PANEL);
        const char *l = tr(L[i]);
        gfx_text(FONT_BOLD, bx + (120 - gfx_text_width(FONT_BOLD, l)) / 2, by + 4, COL_TEXT, l);
    }
}

static void draw_player(const App *a, VideoWin *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    if (s->tex && s->have_frame) {   // fit the screen, proportions kept
        int w = SCREEN_W, h = s->tex_h * SCREEN_W / (s->tex_w ? s->tex_w : 1);
        if (h > SCREEN_H) { h = SCREEN_H; w = s->tex_w * SCREEN_H / (s->tex_h ? s->tex_h : 1); }
        SDL_Rect src = { 0, 0, s->tex_w, s->tex_h }, dst = { (SCREEN_W - w) / 2, (SCREEN_H - h) / 2, w, h };
        SDL_RenderCopy(gfx_renderer(), s->tex, &src, &dst);
    }
    int hud = video_paused() || a->now_ms - s->input_ms < HUD_MS || s->modal != VM_NONE;
    draw_subs(s, hud);
    if (hud && s->modal != VM_RESUME_Q) {
        char name[256], now[24], all[24], line[64];
        title_of(s, name, sizeof name);
        gfx_rect(0, 0, SCREEN_W, 18, 0x00000099);
        gfx_text_fit(FONT_SMALL, 8, 2, SCREEN_W - 80, COL_TEXT, name);
        if (video_paused()) gfx_text(FONT_SMALL, SCREEN_W - 8 - gfx_text_width(FONT_SMALL, tr(S_VID_PAUSED)), 2, COL_WARN, tr(S_VID_PAUSED));
        int64_t pos = video_pos_ms(), dur = s->info.duration_ms;
        fmt_time(pos, now, sizeof now);
        fmt_time(dur, all, sizeof all);
        snprintf(line, sizeof line, "%s / %s", now, all);
        int by = SCREEN_H - HINT_BAR_H - 14;
        gfx_rect(0, by - 6, SCREEN_W, 20, 0x00000099);
        int tw = gfx_text_width(FONT_SMALL, line);
        gfx_round_rect(10, by + 2, SCREEN_W - 30 - tw, 4, 2, 0x3a4456ff);
        if (dur > 0) gfx_round_rect(10, by + 2, (int)((SCREEN_W - 30 - tw) * (pos > dur ? dur : pos) / dur), 4, 2, COL_ACCENT);
        gfx_text(FONT_SMALL, SCREEN_W - 10 - tw, by - 4, COL_TEXT, line);
    }
    if (a->now_ms < s->note_until) {
        int w = gfx_text_width(FONT_SMALL, s->note) + 16;
        gfx_round_rect(SCREEN_W - w - 8, 24, w, 18, 6, 0x000000b0);
        gfx_text(FONT_SMALL, SCREEN_W - w, 26, COL_TEXT, s->note);
    }
    switch (s->modal) {
    case VM_RESUME_Q: draw_resume_q(s); break;
    case VM_MENU: draw_menu(s); break;
    case VM_INFO: draw_info(s); break;
    case VM_NONE: case VM_ERROR: break;
    }
}

static void draw_list(const VideoWin *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    gfx_rect(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR);
    gfx_text(FONT_BOLD, 10, 3, COL_TEXT, tr(S_ORG_VIDEO));
    if (!s->n) {
        const char *m = tr(S_VID_EMPTY);
        gfx_text(FONT_SMALL, (SCREEN_W - gfx_text_width(FONT_SMALL, m)) / 2, SCREEN_H / 2 - 8, COL_MUTED, m);
        return;
    }
    for (int r = 0; r < LIST_ROWS; r++) {
        int i = s->top + r;
        if (i >= s->n) break;
        const VidItem *it = &s->items[i];
        int y = TOP_BAR_H + 4 + r * ROW_H;
        if (i == s->sel) gfx_round_rect(6, y, SCREEN_W - 12, ROW_H - 1, 5, 0x24324aff);
        char meta[160] = "", key[VR_KEY_MAX], sz[32];
        vresume_key(key, sizeof key, it->path, it->size);
        const VrEntry *e = vresume_get(&vres, key);
        game_size_text(sz, sizeof sz, it->size, i18n_get() == LANG_EN);
        if (e && e->done) snprintf(meta, sizeof meta, "%s · %s", tr(S_VID_WATCHED), sz);
        else if (vresume_ask(e)) {
            char t[24], st[64];
            fmt_time(e->pos_ms, t, sizeof t);
            snprintf(st, sizeof st, tr(S_VID_STOPPED_FMT), t);
            snprintf(meta, sizeof meta, "%s · %s", st, sz);
        } else snprintf(meta, sizeof meta, "%s", sz);
        int mw = gfx_text_width(FONT_SMALL, meta);
        gfx_text_fit(FONT_SMALL, 14, y + 1, SCREEN_W - 40 - mw, COL_TEXT, it->shown);
        gfx_text(FONT_SMALL, SCREEN_W - 14 - mw, y + 1, e && e->done ? COL_MUTED : vresume_ask(e) ? COL_ACCENT : COL_MUTED, meta);
    }
}

static int small_width(void *ctx, const char *t) { (void)ctx; return gfx_text_width(FONT_SMALL, t); }

static void draw_error(const VideoWin *s) {
    int x, y, w = 440, h = 120;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000a0);
    draw_box(w, h, &x, &y);
    gfx_text_fit(FONT_BOLD, x + 16, y + 14, w - 32, COL_TEXT, tr(S_VID_SORRY));
    gfx_text_fit(FONT_BODY, x + 16, y + 38, w - 32, COL_TEXT, s->err_line);
    char lines[SUBS_MAX_LINES][256];   // the reason may be long: wrapped by words
    int n = s->err_detail[0] ? subs_wrap(s->err_detail, w - 32, small_width, NULL, lines) : 0;
    for (int k = 0; k < n; k++) gfx_text(FONT_SMALL, x + 16, y + 62 + k * 15, COL_MUTED, lines[k]);   // wrapped to fit already
}

void video_win_draw(const App *ac) {
    App *a = (App *)ac;
    VideoWin *s = win(a);
    if (!s) return;
    if (s->view == VW_PLAYER) draw_player(a, s);
    else draw_list(s);
    if (s->modal == VM_ERROR) draw_error(s);
}

void video_win_hints(const App *a) {
    const VideoWin *s = win(a);
    if (!s) return;
    if (s->modal == VM_ERROR) {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
        return;
    }
    if (s->view == VW_LIST) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_PLAY }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, s->n ? 2 : 1);
        return;
    }
    if (s->modal == VM_MENU) {
        static const Hint h[] = { { ACT_LEFT, S_HINT_VALUE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (s->modal != VM_NONE) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (video_paused() || a->now_ms - s->input_ms < HUD_MS) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_PAUSE }, { ACT_LEFT, S_HINT_REWIND }, { ACT_MENU, S_HINT_MENU },
                                  { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 4);
    }
}
