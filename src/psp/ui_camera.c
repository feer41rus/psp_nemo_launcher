// Camera (1.3.6): Organizer › Camera with a Go!Cam (PSP-450). Viewfinder full screen (mirrored when the lens
// looks at you), ✕ photo / recording start-stop, △ photo or video, □ self-timer, L/R zoom, SELECT filter,
// ○ back. Photos 640x480 into PICTURE/Camera, video 480x272 Motion JPEG + microphone into VIDEO/Camera.
// The music waits while the window is open.
#include "ui.h"
#include "camera_hw.h"
#include "camfile.h"
#include "gfx.h"
#include "log.h"
#include "player.h"
#include "usb.h"
#include "platform.h"
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PHOTO_DIR "ms0:/PICTURE/Camera"
#define VIDEO_DIR "ms0:/VIDEO/Camera"

typedef struct CamWin {
    SDL_Texture *tex;
    int have_frame, video_mode, timer_s, music_was;
    CamFilter filter;
    uint32_t countdown_until;   // the self-timer runs (0: off)
    uint32_t note_until, filter_until;
    char note[160];
    int shooting;               // a photo is being taken
} CamWin;

static CamWin *win(const App *a) { return a->cam; }

int cam_win_is_open(const App *a) { return a->cam != NULL; }
int cam_win_busy(const App *a) { return a->cam && (cam_recording() || a->cam->shooting); }

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void now_time(CamTime *t) {
    ScePspDateTime d;
    memset(t, 0, sizeof *t);
    if (sceRtcGetCurrentClockLocalTime(&d) >= 0) {
        t->year = d.year; t->month = d.month; t->day = d.day;
        t->hour = d.hour; t->minute = d.minute; t->second = d.second;
    }
}

static void note(App *a, CamWin *s, const char *text) {
    snprintf(s->note, sizeof s->note, "%s", text);
    s->note_until = a->now_ms + 2500;
}

void cam_win_open(App *a) {
    if (a->cam) return;
    if (usb_active()) { ui_toast(a, tr(S_CAM_USB_BUSY)); return; }   // the USB port is the PC's now
    CamWin *s = calloc(1, sizeof *s);
    if (!s) return;
    a->cam = s;
    s->music_was = player_active() && !player_paused();
    if (s->music_was) player_pause(1);
    s->tex = SDL_CreateTexture(gfx_renderer(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 480, 272);
    if (s->tex) SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_NONE);
    log_msg("camera: window open (music %s)", s->music_was ? "paused" : "not playing");
    cam_start();
}

static void close_win(App *a) {
    CamWin *s = win(a);
    if (!s) return;
    if (cam_recording()) cam_record_stop();
    cam_stop();
    if (s->tex) SDL_DestroyTexture(s->tex);
    if (s->music_was) player_pause(0);
    free(s);
    a->cam = NULL;
    log_msg("camera: window closed");
}

static void shoot(App *a, CamWin *s) {
    CamTime t;
    now_time(&t);
    char path[160];
    mkdir("ms0:/PICTURE", 0777);
    mkdir(PHOTO_DIR, 0777);
    if (camfile_name(path, sizeof path, PHOTO_DIR, "IMG", "JPG", &t, exists) != 0) { ui_fail(a, S_CAM_SAVE_FAIL); return; }
    s->shooting = 1;
    cam_photo(path);
    log_msg("camera: photo requested %s (filter %d, zoom %d)", path, (int)s->filter, cam_zoom(0));
}

static void toggle_record(App *a, CamWin *s) {
    if (cam_recording()) {
        int rc = cam_record_stop();
        if (rc == 0) note(a, s, tr(S_CAM_VIDEO_SAVED));
        else ui_fail(a, S_CAM_SAVE_FAIL);
        return;
    }
    CamTime t;
    now_time(&t);
    char path[160];
    mkdir("ms0:/VIDEO", 0777);
    mkdir(VIDEO_DIR, 0777);
    if (camfile_name(path, sizeof path, VIDEO_DIR, "MOV", "AVI", &t, exists) != 0 || cam_record_start(path) != 0)
        ui_fail(a, S_CAM_SAVE_FAIL);
}

static void trigger(App *a, CamWin *s) {
    if (s->video_mode) toggle_record(a, s);
    else shoot(a, s);
}

void cam_win_filter_next(App *a) {   // SELECT alone
    CamWin *s = win(a);
    if (!s || cam_state() != CS_READY) return;
    s->filter = camfilter_next(s->filter);
    cam_set_filter(s->filter);
    s->filter_until = a->now_ms + 1500;
    log_dbg("camera: filter %s", tr_en(camfilter_name(s->filter)));
}

void cam_win_action(App *a, Action act) {
    CamWin *s = win(a);
    if (!s) return;
    CamState st = cam_state();
    if (act == ACT_BACK) {
        if (s->countdown_until) { s->countdown_until = 0; return; }   // ○ first stops the timer
        close_win(a);
        return;
    }
    if (st == CS_NO_CAMERA || st == CS_FAILED) {
        if (act == ACT_CONFIRM) { cam_stop(); cam_start(); }   // try again
        return;
    }
    if (st != CS_READY || s->shooting) return;
    switch (act) {
    case ACT_CONFIRM:
        if (s->countdown_until) break;
        if (s->timer_s && !(s->video_mode && cam_recording())) s->countdown_until = a->now_ms + (uint32_t)s->timer_s * 1000;
        else trigger(a, s);
        break;
    case ACT_MENU:
        if (cam_recording() || s->countdown_until) break;
        s->video_mode = !s->video_mode;
        log_dbg("camera: mode %s", s->video_mode ? "video" : "photo");
        break;
    case ACT_QUICK:
        if (s->countdown_until) break;
        s->timer_s = s->timer_s == 0 ? 3 : s->timer_s == 3 ? 10 : 0;
        break;
    case ACT_TAB_PREV: cam_zoom(-2); break;   // L
    case ACT_TAB_NEXT: cam_zoom(2); break;    // R
    default: break;
    }
}

void cam_win_tick(App *a) {
    CamWin *s = win(a);
    if (!s) return;
    if (a->nav.tab != TAB_ORGANIZER) { close_win(a); return; }
    const uint32_t *px;
    int w, h;
    if (cam_take_frame(&px, &w, &h)) {
        if (s->tex) SDL_UpdateTexture(s->tex, NULL, px, w * 4);
        cam_frame_done();
        s->have_frame = 1;
    }
    if (s->countdown_until && a->now_ms >= s->countdown_until) {
        s->countdown_until = 0;
        trigger(a, s);
    }
    int rc;
    if (cam_take_photo_result(&rc)) {
        s->shooting = 0;
        if (rc == 0) note(a, s, tr(S_CAM_PHOTO_SAVED));
        else ui_fail(a, S_CAM_SAVE_FAIL);
    }
    if (cam_record_take_stopped()) ui_fail(a, S_CAM_REC_STOPPED);
}

static void fmt_ms(uint32_t ms, char *out, size_t cap) {
    uint32_t sec = ms / 1000;
    snprintf(out, cap, "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

static void centered(FontId f, int y, uint32_t col, const char *t) { gfx_text(f, (SCREEN_W - gfx_text_width(f, t)) / 2, y, col, t); }

void cam_win_draw(const App *ac) {
    App *a = (App *)ac;
    CamWin *s = win(a);
    if (!s) return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000ff);
    CamState st = cam_state();
    if (st == CS_CONNECTING || st == CS_OFF) { centered(FONT_BODY, 120, COL_TEXT, tr(S_CAM_CONNECTING)); return; }
    if (st == CS_NO_CAMERA || st == CS_FAILED) {
        centered(FONT_BOLD, 100, COL_TEXT, tr(st == CS_NO_CAMERA ? S_CAM_NONE : S_CAM_FAILED));
        centered(FONT_SMALL, 128, COL_MUTED, tr(S_CAM_NONE_HINT));
        return;
    }
    if (s->have_frame && s->tex) {
        SDL_Rect dst = { 0, 0, SCREEN_W, SCREEN_H };
        // selfie: like a mirror (author's choice); the files stay as the camera sees
        SDL_RenderCopyEx(gfx_renderer(), s->tex, NULL, &dst, 0, NULL, cam_lens_toward_user() ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
    // top line: mode, filter, zoom, timer, recording
    gfx_rect(0, 0, SCREEN_W, 22, 0x00000080);
    char line[96];
    int z = cam_zoom(0);
    snprintf(line, sizeof line, "%s · %s · ×%d.%d", tr(s->video_mode ? S_CAM_MODE_VIDEO : S_CAM_MODE_PHOTO), tr(camfilter_name(s->filter)),
             z / 10, z % 10);
    gfx_text(FONT_SMALL, 8, 4, COL_TEXT, line);
    if (s->timer_s) {
        snprintf(line, sizeof line, tr(S_CAM_TIMER_FMT), s->timer_s);
        gfx_text(FONT_SMALL, SCREEN_W - 8 - gfx_text_width(FONT_SMALL, line), 4, COL_ACCENT, line);
    }
    if (cam_recording()) {
        char t[16];
        fmt_ms(cam_record_ms(), t, sizeof t);
        int x = SCREEN_W / 2 - 30;
        if ((a->now_ms / 500) & 1) gfx_disc(x, 11, 5, 0xe0303aff);
        gfx_text(FONT_SMALL, x + 10, 4, COL_TEXT, t);
    }
    if (s->countdown_until) {
        char n[8];
        snprintf(n, sizeof n, "%u", (unsigned)((s->countdown_until - a->now_ms + 999) / 1000));
        centered(FONT_TITLE, 110, 0xffffffff, n);
    }
    if (s->shooting) centered(FONT_BODY, 230, COL_TEXT, tr(S_CAM_SHOOTING));
    if (a->now_ms < s->filter_until) centered(FONT_BOLD, 120, 0xffffffff, tr(camfilter_name(s->filter)));
    if (a->now_ms < s->note_until) {
        int w = gfx_text_width(FONT_SMALL, s->note) + 24;
        gfx_round_rect((SCREEN_W - w) / 2, 200, w, 24, 10, 0x1a2230d8);
        centered(FONT_SMALL, 205, COL_TEXT, s->note);
    }
}

void cam_win_hints(const App *a) {
    const CamWin *s = win(a);
    if (!s) return;
    CamState st = cam_state();
    if (st != CS_READY) {
        static const Hint h[] = { { ACT_CONFIRM, S_CAM_RETRY }, { ACT_BACK, S_HINT_BACK } };
        int retry = st == CS_NO_CAMERA || st == CS_FAILED;
        ui_hints_draw(a, retry ? h : h + 1, retry ? 2 : 1);
        return;
    }
    Hint h[6];
    int n = 0;
    h[n++] = (Hint){ ACT_CONFIRM, s->video_mode ? (cam_recording() ? S_CAM_STOP : S_CAM_RECORD) : S_CAM_SHOOT };
    if (!cam_recording()) h[n++] = (Hint){ ACT_MENU, s->video_mode ? S_CAM_MODE_PHOTO : S_CAM_MODE_VIDEO };
    h[n++] = (Hint){ ACT_QUICK, S_CAM_TIMER };
    h[n++] = (Hint){ ACT_SELECT, S_CAM_FILTER };
    h[n++] = (Hint){ ACT_BACK, S_HINT_BACK };
    ui_hints_draw(a, h, n);
}

const char *cam_screen(const App *a) {
    const CamWin *s = win(a);
    if (!s) return "closed";
    return cam_recording() ? "recording" : s->video_mode ? "video" : "photo";
}
