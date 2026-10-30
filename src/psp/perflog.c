// Log analytics (1.2.6): which screen is open, a summary line per visit (fps, worst frame, free RAM in
// and out), slow frames with what was going on, and "time:" lines for long operations. One RAM measure
// everywhere: platform_memory() (Nemo's heap + kernel free), the same numbers as the debug box.
#include "ui.h"
#include "art.h"
#include "log.h"
#include "memory_hw.h"
#include "platform.h"
#include "player.h"
#include "scrstat.h"
#include <stdio.h>
#include <string.h>

static ScrStat cur;
static int started;
static uint32_t last_slow_log;

static void add(char *out, size_t cap, const char *part) {
    size_t n = strlen(out);
    if (n < cap) snprintf(out + n, cap - n, "%s", part);
}

static const char *music_screen(const App *a) {
    static const char *const VIEWS[] = { "albums", "artists", "folders", "all_tracks" };
    switch (mnav_top(&a->mnav)) {
    case MS_LIBRARY: return a->mnav.view >= 0 && a->mnav.view < 4 ? VIEWS[a->mnav.view] : "library";
    case MS_NOW: return "now_playing";
    case MS_MENU: return "menu";
    case MS_QUEUE: return "queue";
    case MS_PICK: return "playlist_pick";
    case MS_OSK: return "keyboard";
    case MS_CONFIRM: return "confirm";
    case MS_LISTEN: return "listening";
    case MS_EQ: return "equalizer";
    }
    return "?";
}

void perflog_name(const App *a, char *out, size_t cap) {
    static const char *const TABS[TAB_COUNT] = { "home", "library", "organizer", "music", "network", "settings" };
    static const char *const NETS[] = { "wifi", "keyboard", "menu", "forget", "rescan", "usb", "ftp" };
    out[0] = '\0';
    if (a->splash_active) { add(out, cap, "splash"); return; }
    add(out, cap, TABS[a->nav.tab < TAB_COUNT ? a->nav.tab : 0]);
    add(out, cap, "/");
    switch (a->nav.tab) {
    case TAB_MUSIC: add(out, cap, music_screen(a)); break;
    case TAB_NETWORK: add(out, cap, (unsigned)a->net_screen < 7 ? NETS[a->net_screen] : "?"); break;
    case TAB_SETTINGS: add(out, cap, tr_en(setmenu_entry(setmenu_node(&a->setmenu))->label)); break;
    case TAB_ORGANIZER:
        if (photo_tab_owns_buttons(a)) add(out, cap, "gallery");
        else if (video_win_is_open(a)) { add(out, cap, "video/"); add(out, cap, video_screen(a)); }
        else if (cam_win_is_open(a)) { add(out, cap, "camera/"); add(out, cap, cam_screen(a)); }
        else if (files_is_open(a)) { add(out, cap, "files/"); add(out, cap, files_screen(a)); }
        else add(out, cap, "list");
        break;
    default: {
        ScreenKind k = nav_top(&a->nav);
        add(out, cap, k == SCR_CARD ? "card" : k == SCR_ROOT ? "shelves" : "dialog");
        break;
    }
    }
    // Screens over the tab, in the order main.c draws them.
    if (a->gm_open) add(out, cap, "+game_menu");
    if (a->plug) add(out, cap, "+plugins");
    if (a->page) add(out, cap, "+text");
    if (a->saves) add(out, cap, "+saves");
    if (a->battery) add(out, cap, battery_running(a) ? "+battery_test" : "+battery");
    if (a->memory) add(out, cap, "+memory");
    if (a->dt_open) add(out, cap, "+date_editor");
    if (a->pick_open) add(out, cap, "+pick");
    if (a->power.open) add(out, cap, "+power");
    if (a->listening) add(out, cap, "+listening");
}

static uint32_t ram_free_now(void) {
    uint32_t free_b, total_b;
    platform_memory(&free_b, &total_b);
    return free_b;
}

void perflog_frame(App *a, uint32_t frame_ms) {
    char name[64];
    perflog_name(a, name, sizeof name);
    if (!started || !scrstat_same(&cur, name)) {
        char line[200];
        uint32_t ram = ram_free_now();   // mallinfo walks the heap: only on a screen change
        if (started && scrstat_summary(&cur, a->now_ms, ram, line, sizeof line)) log_msg("%s", line);
        scrstat_begin(&cur, name, a->now_ms, ram);
        log_dbg("screen: -> %s (ram free %u KB)", name, (unsigned)(ram / 1024));
        started = 1;
        return;   // the first frame of a screen includes its opening work: not counted
    }
    if (scrstat_frame(&cur, frame_ms) && a->now_ms - last_slow_log >= 1000) {   // at most one line a second
        last_slow_log = a->now_ms;
        log_msg("slow: %u ms on %s (music=%d art=%d mem_scan=%d)", (unsigned)frame_ms, cur.name, player_active(),
                art_busy(), memory_scan_busy());
    }
}

void perflog_second(const App *a, int fps) {
    scrstat_second(&cur, fps);
    if (fps < 50) log_msg("perf: %d fps on %s, ram_free=%uKB", fps, cur.name, (unsigned)(a->ram_free / 1024));
}

const char *perflog_screen(void) { return started ? cur.name : ""; }

void log_time(const char *what, uint32_t ms) { log_msg("time: %s %u ms", what, (unsigned)ms); }
