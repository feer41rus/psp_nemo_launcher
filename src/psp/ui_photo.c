// Gallery (1.3.1 Photos tab, inside the Organizer since 1.3.2): a grid of thumbnails, a full-screen viewer (browse, zoom and move, rotate into
// the file, delete, info). Large photos rotate in bands on a thread behind a waiting screen.
#include "ui.h"
#include "art.h"
#include "gfx.h"
#include "gamesize.h"
#include "log.h"
#include "libbg.h"
#include "libbg_hw.h"
#include "photo_hw.h"
#include "photos.h"
#include "photoview.h"
#include "sjis.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include <sys/stat.h>
#include <time.h>

#define COLS 5
#define CELL_W 84
#define CELL_H 63
#define GAP_X 8
#define ROW_PITCH 69
#define GRID_X 14
#define GRID_Y (CONTENT_Y + 22)
#define ROWS_SHOWN 3
#define PAN_STEP 40
#define HINT_HIDE_MS 3000

typedef enum { PM_NONE, PM_MENU, PM_DELETE_Q, PM_BIG_Q, PM_INFO } PhotoModal;
typedef enum { PI_ROT_RIGHT, PI_ROT_LEFT, PI_LIB_BG, PI_DELETE, PI_INFO } PhotoItem;

typedef struct PhotoTab {
    int window;                 // the Gallery window is open
    int from_files;             // opened on one picture from the file manager: its folder only, ○ goes back
    char last_path[256];        // from_files: the picture shown last (the manager puts its cursor there)
    PhotoList list;
    int loaded, sel, top_row;
    int scanning, list_dirty;   // the list is built on a thread; kept while away from the tab
    PhotoList scan_list;
    volatile int scan_done;
    int load_pending, load_drawn;   // viewer: "loading…" is shown for a frame before the decode
    int viewing;
    PhotoModal modal;
    int menu_sel, yes;
    PhotoItem items[5];
    int n_items;
    // viewer
    PhotoImage img;
    int img_for;                // index the image was loaded for, -1 none
    int load_rc;                // 0 ok, -1 failed, -2 too large
    float zooms[5];
    int n_zoom, zi, pan_x, pan_y;
    SDL_Texture *tex;
    uint32_t *px;               // 480x272 frame for the texture, only while the viewer is open
    int dirty;
    uint32_t input_ms;
    // rotation
    int rot_pending;            // 1 cw, 2 ccw: done after a frame with "Rotating…" on screen
    int rot_drawn;
    int bg_pending, bg_drawn;   // 1.3.8: "Set as Library background" runs after a frame with a note on screen
    int big_running, big_cw, big_w, big_h;
} PhotoTab;

static PhotoTab *tab_state(App *a) {
    if (!a->photo) a->photo = calloc(1, sizeof *a->photo);
    return a->photo;
}

static void request_load(PhotoTab *s);

static PhotoTab *scan_for;
static uint32_t scan_t0;

static int scan_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    static const char *const ROOTS[] = { "ms0:/PICTURE", "ms0:/DCIM" };
    photos_scan_ex(&scan_for->scan_list, ROOTS, 2, photo_leftover, NULL);
    scan_for->scan_done = 1;
    sceKernelExitDeleteThread(0);
    return 0;
}

static void start_scan(PhotoTab *s) {
    if (s->scanning) return;
    scan_for = s;
    scan_t0 = platform_ms();
    s->scan_done = 0;
    s->scanning = 1;
    s->list_dirty = 0;
    SceUID th = sceKernelCreateThread("nemo_photo_scan", scan_thread, 0x50, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {   // fall back to scanning here
        static const char *const ROOTS[] = { "ms0:/PICTURE", "ms0:/DCIM" };
        photos_scan_ex(&s->scan_list, ROOTS, 2, photo_leftover, NULL);
        s->scan_done = 1;
    }
}

static void clamp_view(PhotoTab *s) {
    if (s->sel >= s->list.count) s->sel = s->list.count ? s->list.count - 1 : 0;
    int row = s->sel / COLS, last = s->list.count ? (s->list.count - 1) / COLS : 0;
    if (s->top_row > row) s->top_row = row;
    if (row >= s->top_row + ROWS_SHOWN) s->top_row = row - ROWS_SHOWN + 1;
    if (s->top_row > last - ROWS_SHOWN + 1) s->top_row = last - ROWS_SHOWN + 1;   // no empty rows below
    if (s->top_row < 0) s->top_row = 0;
}

static void take_scan(PhotoTab *s) {
    photos_free(&s->list);
    s->list = s->scan_list;
    memset(&s->scan_list, 0, sizeof s->scan_list);
    s->scanning = 0;
    if (!s->window) { photos_free(&s->list); return; }   // closed meanwhile: nothing kept
    s->loaded = 1;
    clamp_view(s);
    if (s->viewing) request_load(s);   // indices may have moved
    log_msg("photo: %d photos", s->list.count);
    log_time("photo_scan", platform_ms() - scan_t0);
}

static void free_view(PhotoTab *s) {
    photo_image_free(&s->img);
    if (s->tex) SDL_DestroyTexture(s->tex);
    s->tex = NULL;
    free(s->px);
    s->px = NULL;
    s->img_for = -1;
}

static StrId source_name(PhotoSource src) {
    switch (src) {
    case PS_GAMES: return S_PH_SRC_GAMES;
    case PS_NEMO: return S_PH_SRC_NEMO;
    case PS_CAMERA: return S_PH_SRC_CAMERA;
    case PS_UPLOADED: break;
    }
    return S_PH_SRC_UPLOADED;
}

// The file name for the screen: the card lists names in Shift-JIS (rotating buffer, two per call).
static const char *base_name(const char *path) {
    static char buf[2][400];
    static int k;
    const char *s = strrchr(path, '/');
    char *b = buf[k++ & 1];
    sjis_to_utf8(s ? s + 1 : path, b, sizeof buf[0]);
    return b;
}

static void date_text(long mtime, char *out, size_t cap) {
    time_t t = (time_t)mtime;
    struct tm *tm = localtime(&t);
    if (tm) strftime(out, cap, "%d.%m.%Y %H:%M", tm);
    else out[0] = '\0';
}

// ---------- viewer ----------

static void load_current(PhotoTab *s) {
    free_view(s);
    if (s->sel < 0 || s->sel >= s->list.count) return;
    s->load_rc = photo_load(s->list.items[s->sel].path, PHOTO_VIEW_MAX, &s->img);
    s->img_for = s->sel;
    s->zi = 0;
    s->pan_x = s->pan_y = 0;
    s->n_zoom = s->load_rc == 0 ? pv_zoom_steps(s->img.w, s->img.h, SCREEN_W, SCREEN_H, s->zooms) : 1;
    s->zooms[0] = 1.0f;
    s->dirty = 1;
}

// The visible 480x272 part of the photo at the current zoom and pan, sampled into a texture.
static void render_view(PhotoTab *s) {
    static int xmap[SCREEN_W];
    if (!s->img.rgb) return;
    if (!s->px) s->px = malloc(SCREEN_W * SCREEN_H * 4);
    if (!s->px) return;
    uint32_t *px = s->px;
    if (!s->tex) s->tex = SDL_CreateTexture(gfx_renderer(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
    if (!s->tex) return;
    PvRect f = pv_fit(s->img.w, s->img.h, SCREEN_W, SCREEN_H);
    float z = s->zooms[s->zi];
    int dw = (int)(f.w * z + 0.5f), dh = (int)(f.h * z + 0.5f);
    int ox = (SCREEN_W - dw) / 2 - s->pan_x, oy = (SCREEN_H - dh) / 2 - s->pan_y;
    for (int x = 0; x < SCREEN_W; x++) {
        int u = x - ox;
        xmap[x] = u >= 0 && u < dw ? (int)((long long)u * s->img.w / dw) : -1;
    }
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *o = px + y * SCREEN_W;
        int v = y - oy;
        if (v < 0 || v >= dh) { memset(o, 0, SCREEN_W * 4); continue; }
        const uint8_t *row = s->img.rgb + (size_t)((long long)v * s->img.h / dh) * s->img.w * 3;
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = xmap[x];
            if (sx < 0) { o[x] = 0xff000000u; continue; }
            const uint8_t *p = row + sx * 3;
            o[x] = 0xff000000u | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
        }
    }
    SDL_UpdateTexture(s->tex, NULL, px, SCREEN_W * 4);
    s->dirty = 0;
}

// The decode happens in the tick after a frame with "loading…" on screen: no frozen old photo.
static void request_load(PhotoTab *s) {
    free_view(s);
    s->load_pending = 1;
    s->load_drawn = 0;
}

static void open_viewer(App *a, PhotoTab *s) {
    if (!s->list.count) return;
    s->viewing = 1;
    s->input_ms = a->now_ms;
    request_load(s);
}

static void close_window(App *a, PhotoTab *s);

static void close_viewer(PhotoTab *s) {
    if (s->from_files && s->sel >= 0 && s->sel < s->list.count)
        snprintf(s->last_path, sizeof s->last_path, "%s", s->list.items[s->sel].path);
    s->viewing = 0;
    s->modal = PM_NONE;
    s->load_pending = 0;
    free_view(s);
}

// ---------- actions shared by grid and viewer ----------

static void after_change(App *a, PhotoTab *s, int focus) {
    if (focus < 0) {
        close_viewer(s);
        s->sel = 0;
        s->top_row = 0;
        if (s->from_files) close_window(a, s);   // the folder has no picture left
        return;
    }
    s->sel = focus;
    clamp_view(s);
    if (s->viewing) request_load(s);
}

static void do_delete(App *a, PhotoTab *s) {
    if (s->sel >= s->list.count) return;
    if (photo_delete(&s->list.items[s->sel]) != 0) { ui_fail(a, S_PH_DEL_FAIL); return; }
    ui_toast(a, tr(S_PH_DELETED));
    after_change(a, s, photos_remove(&s->list, s->sel));
}

static void rotate_done(App *a, PhotoTab *s) {
    Photo *p = &s->list.items[s->sel];
    struct stat st;
    if (stat(p->path, &st) == 0) p->size = (long long)st.st_size;   // new size: a new thumbnail key
    (void)a;
    if (s->viewing) request_load(s);
}

static void start_rotate(App *a, PhotoTab *s, int cw) {
    (void)a;
    if (s->big_running || s->sel >= s->list.count) return;
    s->rot_pending = cw ? 1 : 2;
    s->rot_drawn = 0;
}

static void run_pending_rotate(App *a, PhotoTab *s) {
    int cw = s->rot_pending == 1;
    s->rot_pending = 0;
    const char *path = s->list.items[s->sel].path;
    if (s->viewing) free_view(s);   // the memory of the viewer is needed for the rotation
    PhotoRotateResult r = photo_rotate_file(path, cw);
    if (r == PR_OK) { rotate_done(a, s); return; }
    if (r == PR_BIG) {
        int w = 0, h = 0;
        photo_size(path, &w, &h);
        s->big_cw = cw;
        s->big_w = w;
        s->big_h = h;
        s->modal = PM_BIG_Q;
        s->yes = 0;
    } else {
        ui_fail(a, S_PH_ROT_FAIL);
    }
    if (s->viewing && s->img_for < 0) request_load(s);
}

// 1.3.8: the picture becomes the Library's background (cut to the screen under a matte coat).
static void run_pending_bg(App *a, PhotoTab *s) {
    s->bg_pending = 0;
    if (s->sel >= s->list.count) return;
    if (s->viewing) free_view(s);   // the decode needs the viewer's memory
    if (libbg_make(s->list.items[s->sel].path) == 0) {
        a->lib->settings.lib_bg = LIBBG_OWN;
        library_save(a->lib, NEMO_DATA_PATH);
        ui_toast(a, tr(S_PH_LIB_BG_SET));
    } else {
        ui_fail(a, S_PH_LIB_BG_FAIL);
    }
    if (s->viewing && s->img_for < 0) request_load(s);
}

static void open_menu(PhotoTab *s) {
    s->n_items = 0;
    if (s->viewing) {
        s->items[s->n_items++] = PI_ROT_RIGHT;
        s->items[s->n_items++] = PI_ROT_LEFT;
    }
    s->items[s->n_items++] = PI_LIB_BG;
    s->items[s->n_items++] = PI_DELETE;
    if (s->viewing) s->items[s->n_items++] = PI_INFO;
    s->menu_sel = 0;
    s->modal = PM_MENU;
}

static StrId item_label(PhotoItem it) {
    switch (it) {
    case PI_ROT_RIGHT: return S_PH_ROT_RIGHT;
    case PI_ROT_LEFT: return S_PH_ROT_LEFT;
    case PI_LIB_BG: return S_PH_SET_LIB_BG;
    case PI_DELETE: return S_PH_DELETE;
    case PI_INFO: break;
    }
    return S_PH_INFO;
}

static void modal_action(App *a, PhotoTab *s, Action act) {
    switch (s->modal) {
    case PM_MENU:
        if (act == ACT_BACK || act == ACT_MENU) { s->modal = PM_NONE; return; }
        if (act == ACT_UP && s->menu_sel > 0) s->menu_sel--;
        if (act == ACT_DOWN && s->menu_sel + 1 < s->n_items) s->menu_sel++;
        if (act != ACT_CONFIRM) return;
        s->modal = PM_NONE;
        switch (s->items[s->menu_sel]) {
        case PI_ROT_RIGHT: start_rotate(a, s, 1); break;
        case PI_ROT_LEFT: start_rotate(a, s, 0); break;
        case PI_LIB_BG: s->bg_pending = 1; s->bg_drawn = 0; break;
        case PI_DELETE: s->modal = PM_DELETE_Q; s->yes = 0; break;
        case PI_INFO: s->modal = PM_INFO; break;
        }
        return;
    case PM_DELETE_Q:
    case PM_BIG_Q:
        if (act == ACT_LEFT || act == ACT_RIGHT || act == ACT_UP || act == ACT_DOWN) s->yes = !s->yes;
        if (act == ACT_BACK) { s->modal = PM_NONE; return; }
        if (act != ACT_CONFIRM) return;
        {
            PhotoModal m = s->modal;
            s->modal = PM_NONE;
            if (!s->yes) return;
            if (m == PM_DELETE_Q) { do_delete(a, s); return; }
            if (s->viewing) free_view(s);
            PhotoRotateResult r = photo_rotate_big_start(s->list.items[s->sel].path, s->big_w, s->big_h, s->big_cw);
            if (r == PR_OK) s->big_running = 1;
            else ui_toast(a, tr(r == PR_NO_SPACE ? S_PH_NO_SPACE : S_PH_ROT_FAIL));
            if (!s->big_running && s->viewing) request_load(s);
        }
        return;
    case PM_INFO:
        if (act == ACT_BACK || act == ACT_CONFIRM || act == ACT_MENU) s->modal = PM_NONE;
        return;
    case PM_NONE:
        return;
    }
}

static void viewer_action(App *a, PhotoTab *s, Action act) {
    s->input_ms = a->now_ms;
    float z = s->zooms[s->zi];
    switch (act) {
    case ACT_BACK:
        close_viewer(s);
        if (s->from_files) close_window(a, s);   // straight back to the file manager
        return;
    case ACT_MENU: open_menu(s); return;
    case ACT_QUICK: start_rotate(a, s, 1); return;
    case ACT_TAB_NEXT: if (s->zi + 1 < s->n_zoom) { s->zi++; s->dirty = 1; } break;
    case ACT_TAB_PREV: if (s->zi > 0) { s->zi--; s->dirty = 1; } break;
    case ACT_LEFT:
    case ACT_RIGHT:
        if (z <= 1.0f) {   // browse
            int n = s->sel + (act == ACT_RIGHT ? 1 : -1);
            if (n >= 0 && n < s->list.count) { s->sel = n; request_load(s); }
            return;
        }
        s->pan_x += act == ACT_RIGHT ? PAN_STEP : -PAN_STEP;
        s->dirty = 1;
        break;
    case ACT_UP: if (z > 1.0f) { s->pan_y -= PAN_STEP; s->dirty = 1; } break;
    case ACT_DOWN: if (z > 1.0f) { s->pan_y += PAN_STEP; s->dirty = 1; } break;
    default: break;
    }
    if (s->img.rgb) pv_clamp_pan(s->img.w, s->img.h, SCREEN_W, SCREEN_H, s->zooms[s->zi], &s->pan_x, &s->pan_y);
}

static void grid_action(App *a, PhotoTab *s, Action act) {
    int n = s->list.count;
    switch (act) {
    case ACT_LEFT: if (s->sel > 0) s->sel--; break;
    case ACT_RIGHT: if (s->sel + 1 < n) s->sel++; break;
    case ACT_UP: if (s->sel >= COLS) s->sel -= COLS; break;
    case ACT_DOWN: if (s->sel + COLS < n) s->sel += COLS; else if (s->sel / COLS < (n - 1) / COLS) s->sel = n - 1; break;
    case ACT_CONFIRM: open_viewer(a, s); return;
    case ACT_MENU: if (n) open_menu(s); return;
    default: break;
    }
    int row = s->sel / COLS;
    if (row < s->top_row) s->top_row = row;
    if (row >= s->top_row + ROWS_SHOWN) s->top_row = row - ROWS_SHOWN + 1;
}

// ---------- the window ----------

// Opening frees the game artwork (and its textures): the RAM goes to the photos. Music keeps playing.
static void open_window(App *a, PhotoTab *s) {
    art_reset();
    s->window = 1;
    s->from_files = 0;
    s->sel = s->top_row = 0;
    s->loaded = 0;
    start_scan(s);
    log_msg("photo: window open");
    (void)a;
}

// Closing frees everything of the photos; the Organizer list then brings back Library and Home artwork.
static void close_window(App *a, PhotoTab *s) {
    photo_thumbs_stop();
    close_viewer(s);
    s->rot_pending = 0;
    s->bg_pending = 0;
    photos_free(&s->list);   // the scan thread only fills scan_list
    s->loaded = 0;
    s->window = 0;
    if (s->from_files) s->list_dirty = 1;   // the Gallery's own list is built again when it opens
    s->from_files = 0;
    platform_log_memory("photo-closed");
    (void)a;
}

// ---------- public ----------

void photo_open_file(App *a, const char *dir, const char *name) {
    PhotoTab *s = tab_state(a);
    if (!s || s->window || s->scanning) return;
    photos_folder(&s->list, dir);
    if (!s->list.count) return;
    s->window = 1;
    s->from_files = 1;
    s->loaded = 1;
    s->last_path[0] = '\0';
    s->sel = photos_index_of(&s->list, name);
    s->top_row = 0;
    log_msg("photo: open %s/%s from the file manager (%d in the folder)", dir, name, s->list.count);
    open_viewer(a, s);
}

const char *photo_last_path(const App *a) { return a->photo ? a->photo->last_path : ""; }

void photo_open(App *a) {
    PhotoTab *s = tab_state(a);
    if (s && !s->window) open_window(a, s);
}

int photo_tab_owns_buttons(const App *a) {
    const PhotoTab *s = a->photo;
    return s && (s->window || s->big_running);
}

int photo_tab_fullscreen(const App *a) {
    const PhotoTab *s = a->photo;
    return a->nav.tab == TAB_ORGANIZER && s && (s->window || s->big_running);
}

int photo_tab_busy(const App *a) { return a->photo && a->photo->big_running; }

void photo_tab_action(App *a, Action act) {
    PhotoTab *s = tab_state(a);
    if (!s) return;
    if (s->big_running) {   // the waiting screen owns every button; only ○ does something
        if (act == ACT_BACK) photo_rotate_big_cancel();
        return;
    }
    if (!s->window) return;
    if (s->rot_pending || s->bg_pending || s->load_pending) return;
    if (s->modal != PM_NONE) { modal_action(a, s, act); return; }
    if (s->viewing) viewer_action(a, s, act);
    else if (act == ACT_BACK) close_window(a, s);
    else grid_action(a, s, act);
}

void photo_tab_invalidate(App *a) {
    if (a->photo) a->photo->list_dirty = 1;
}

void photo_tab_tick(App *a) {
    PhotoTab *s = a->photo;
    if (s && s->scanning && s->scan_done) take_scan(s);
    if (a->nav.tab != TAB_ORGANIZER) {   // left the tab (HOME): the window closes with everything in it
        if (s && s->window && !s->big_running) close_window(a, s);
        return;
    }
    if (!s || !s->window) return;   // the Organizer list brings the menus' artwork back
    if (!s->loaded && !s->scanning) start_scan(s);
    photo_thumbs_pause(s->viewing || s->rot_pending || s->bg_pending || s->big_running);
    photo_thumbs_tick();
    if (s->load_pending && s->load_drawn) { s->load_pending = 0; load_current(s); }
    if (s->rot_pending && s->rot_drawn) run_pending_rotate(a, s);
    if (s->bg_pending && s->bg_drawn) run_pending_bg(a, s);
    if (s->big_running) {
        int pct, st = photo_rotate_big_state(&pct);
        if (st >= 0) {
            s->big_running = 0;
            if (st == PR_OK) rotate_done(a, s);
            else ui_toast(a, tr(st == PR_CANCELLED ? S_PH_CANCELLED : st == PR_NO_SPACE ? S_PH_NO_SPACE : S_PH_ROT_FAIL));
            if (s->viewing && s->img_for < 0) request_load(s);
        }
    }
}

static void draw_box(int w, int h, int *x, int *y) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000a0);
    *x = (SCREEN_W - w) / 2;
    *y = (SCREEN_H - h) / 2;
    gfx_round_rect(*x, *y, w, h, 12, 0x1a2230ff);
}

static void draw_question(const PhotoTab *s) {
    char q[160];
    if (s->modal == PM_BIG_Q) snprintf(q, sizeof q, tr(S_PH_BIG_Q_FMT), s->big_w, s->big_h);
    else snprintf(q, sizeof q, "%s", tr(S_PH_DELETE_Q));
    int x, y, w = 360, h = 104;
    draw_box(w, h, &x, &y);
    gfx_text_fit(FONT_SMALL, x + 16, y + 16, w - 32, COL_TEXT, q);
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int sel = (i == 0) == (s->yes != 0);
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, sel ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *lt = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, lt)) / 2, by + 4, COL_TEXT, lt);
    }
}

static void draw_menu(const PhotoTab *s) {
    int w = 220, h = 16 + s->n_items * 26, x, y;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < s->n_items; i++) {
        int ry = y + 8 + i * 26;
        if (i == s->menu_sel) gfx_round_rect(x + 6, ry, w - 12, 24, 6, 0x24324aff);
        gfx_text(FONT_BODY, x + 18, ry + 3, COL_TEXT, tr(item_label(s->items[i])));
    }
}

static void draw_info(const PhotoTab *s) {
    const Photo *p = &s->list.items[s->sel];
    char folder[256], size[32], res[32], date[32];
    snprintf(folder, sizeof folder, "%s", p->path);
    char *slash = strrchr(folder, '/');
    if (slash) *slash = '\0';
    game_size_text(size, sizeof size, p->size, i18n_get() == LANG_EN);
    if (s->img.full_w) snprintf(res, sizeof res, "%d × %d", s->img.full_w, s->img.full_h);
    else res[0] = '\0';
    date_text(p->mtime, date, sizeof date);
    const StrId labels[5] = { S_PH_I_NAME, S_PH_I_FOLDER, S_PH_I_SIZE, S_PH_I_RES, S_PH_I_DATE };
    char folder_u[400];
    sjis_to_utf8(folder, folder_u, sizeof folder_u);
    const char *values[5] = { base_name(p->path), folder_u, size, res, date };
    int x, y, w = 420, h = 20 + 5 * 18;
    draw_box(w, h, &x, &y);
    for (int i = 0; i < 5; i++) {
        gfx_text(FONT_SMALL, x + 14, y + 10 + i * 18, COL_MUTED, tr(labels[i]));
        gfx_text_fit(FONT_SMALL, x + 130, y + 10 + i * 18, w - 144, COL_TEXT, values[i]);
    }
}

static void draw_waiting(const App *a, const PhotoTab *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2 - 16;
    for (int i = 0; i < 8; i++) {   // spinner: eight dots, the bright one goes round
        float ang = (float)i * 0.785398f;
        int lit = (int)(a->now_ms / 100 % 8);
        int d = (i - lit + 8) % 8;
        uint32_t alpha = d == 0 ? 0xff : d == 7 ? 0xb0 : d == 6 ? 0x70 : 0x30;
        gfx_disc(cx + (int)(18 * SDL_cosf(ang)), cy + (int)(18 * SDL_sinf(ang)), 4, 0x6fb0ff00u | alpha);
    }
    char line[64];
    if (s->big_running) {
        int pct;
        photo_rotate_big_state(&pct);
        snprintf(line, sizeof line, tr(S_PH_ROTATING_FMT), pct);
    } else {
        snprintf(line, sizeof line, "%s", tr(S_PH_ROTATING));
    }
    gfx_text(FONT_BODY, cx - gfx_text_width(FONT_BODY, line) / 2, cy + 34, COL_TEXT, line);
}

static void draw_viewer(const App *a, PhotoTab *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    if (s->load_pending || (s->img_for != s->sel && !s->rot_pending)) {
        if (!s->load_pending) request_load(s);
        const char *m = tr(S_PH_LOADING);
        gfx_text(FONT_BODY, (SCREEN_W - gfx_text_width(FONT_BODY, m)) / 2, SCREEN_H / 2 - 8, COL_MUTED, m);
        s->load_drawn = 1;
    } else if (s->load_rc != 0) {
        const char *m = tr(s->load_rc == -2 ? S_PH_TOO_LARGE : S_PH_LOAD_FAIL);
        gfx_text(FONT_BODY, (SCREEN_W - gfx_text_width(FONT_BODY, m)) / 2, SCREEN_H / 2 - 8, COL_MUTED, m);
    } else {
        if (s->dirty) render_view(s);
        if (s->tex) gfx_texture(s->tex, 0, 0, SCREEN_W, SCREEN_H, 255);
    }
    if (a->now_ms - s->input_ms < HINT_HIDE_MS) {   // name and position while the hints are up
        char top[300];
        snprintf(top, sizeof top, "%s · %d / %d", base_name(s->list.items[s->sel].path), s->sel + 1, s->list.count);
        gfx_rect(0, 0, SCREEN_W, 18, 0x00000090);
        gfx_text_fit(FONT_SMALL, 8, 2, SCREEN_W - 16, COL_TEXT, top);
    }
}

static void draw_grid(const App *a, PhotoTab *s) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    if (!s->list.count) {
        const char *m = tr(s->loaded ? S_PH_EMPTY : S_PH_LOADING);
        gfx_text_fit(FONT_SMALL, 16, CONTENT_Y + CONTENT_H / 2 - 8, SCREEN_W - 32, COL_MUTED, m);
        return;
    }
    const Photo *cur = &s->list.items[s->sel];
    char date[32], line[320];
    date_text(cur->mtime, date, sizeof date);
    snprintf(line, sizeof line, "%s · %s · %s", base_name(cur->path), tr(source_name(cur->src)), date);
    gfx_text_fit(FONT_SMALL, GRID_X, CONTENT_Y + 4, SCREEN_W - 2 * GRID_X, COL_TEXT, line);
    for (int r = 0; r < ROWS_SHOWN; r++)
        for (int c = 0; c < COLS; c++) {
            int i = (s->top_row + r) * COLS + c;
            if (i >= s->list.count) break;
            int x = GRID_X + c * (CELL_W + GAP_X), y = GRID_Y + r * ROW_PITCH;
            SDL_Texture *t = photo_thumb(&s->list.items[i]);
            if (t) gfx_texture(t, x, y, CELL_W, CELL_H, 255);
            else gfx_rect(x, y, CELL_W, CELL_H, COL_PANEL);
            if (i == s->sel) {
                gfx_round_rect_outline(x - 2, y - 2, CELL_W + 4, CELL_H + 4, 4, COL_ACCENT);
                gfx_round_rect_outline(x - 3, y - 3, CELL_W + 6, CELL_H + 6, 5, COL_ACCENT);
            }
        }
    (void)a;
}

static void draw_header(const PhotoTab *s) {   // the window's own top line (no tab bar in it)
    gfx_rect(0, 0, SCREEN_W, TOP_BAR_H, COL_BAR);
    gfx_text(FONT_BOLD, 10, 3, COL_TEXT, tr(S_PH_TITLE));
    if (s->loaded) {
        char n[16];
        snprintf(n, sizeof n, "%d", s->list.count);
        gfx_text(FONT_SMALL, SCREEN_W - 10 - gfx_text_width(FONT_SMALL, n), 4, COL_MUTED, n);
    }
}

void photo_tab_draw(const App *ac) {
    App *a = (App *)ac;   // drawing loads the viewer image lazily
    PhotoTab *s = tab_state(a);
    if (!s) return;
    if (!s->window && !s->big_running) return;
    if (s->big_running || (s->rot_pending && s->viewing)) {
        draw_waiting(a, s);
        if (s->rot_pending) s->rot_drawn = 1;
        return;
    }
    if (s->viewing) draw_viewer(a, s);
    else { draw_grid(a, s); draw_header(s); }
    if (s->rot_pending) {   // from the grid: a small note over it
        int x, y;
        draw_box(200, 40, &x, &y);
        gfx_text(FONT_BODY, x + (200 - gfx_text_width(FONT_BODY, tr(S_PH_ROTATING))) / 2, y + 10, COL_TEXT, tr(S_PH_ROTATING));
        s->rot_drawn = 1;
    }
    if (s->bg_pending) {   // a second or two of decoding: a note so the frozen frame is explained
        const char *m = tr(S_PH_LIB_BG_MAKING);
        int x, y, w = gfx_text_width(FONT_BODY, m) + 40;
        draw_box(w, 40, &x, &y);
        gfx_text(FONT_BODY, x + 20, y + 10, COL_TEXT, m);
        s->bg_drawn = 1;
    }
    switch (s->modal) {
    case PM_MENU: draw_menu(s); break;
    case PM_DELETE_Q: case PM_BIG_Q: draw_question(s); break;
    case PM_INFO: draw_info(s); break;
    case PM_NONE: break;
    }
}

void photo_tab_hints(const App *a) {
    const PhotoTab *s = a->photo;
    if (!s || (!s->window && !s->big_running)) return;
    if (s->big_running) {
        static const Hint h[] = { { ACT_BACK, S_HINT_CANCEL } };
        ui_hints_draw(a, h, 1);
    } else if (s->modal == PM_MENU || s->modal == PM_DELETE_Q || s->modal == PM_BIG_Q) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (s->modal == PM_INFO) {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
    } else if (s->viewing) {
        if (a->now_ms - s->input_ms >= HINT_HIDE_MS) return;   // the photo alone after 3 s
        int zoomed = s->zooms[s->zi] > 1.0f;
        Hint h[5] = { { ACT_LEFT, zoomed ? S_PH_MOVE : S_PH_PREV_NEXT }, { ACT_TAB_NEXT, S_PH_ZOOM },
                      { ACT_QUICK, S_PH_ROTATE }, { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 5);
    } else if (s->list.count) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_OPEN }, { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 3);
    } else {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
    }
}
