// Music player glue: library, queue, playback engine, saved state, menus and the ♪ / remote controls.
#include "ui.h"
#include "log.h"
#include "platform.h"
#include "bgsave.h"
#include "hwmp3.h"
#include "art.h"
#include "player.h"
#include "power.h"
#include "m3u.h"
#include "sjis.h"
#include "safefile.h"
#include <sys/stat.h>
#include <psphprm.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_ROOT "ms0:/MUSIC"
#define MUSIC_SCAN_CACHE "ms0:/PSP/NEMO/music_scan.json"
#define MUSIC_DATA "ms0:/PSP/NEMO/music.json"
#define SAVE_EVERY_MS 30000
#define SEEK_STEP_MS 10000
#define LISTEN_DELAY_MS 3000
#define M3U_DIR MUSIC_ROOT "/Playlists"
#define M3U_MAX_BYTES (256 * 1024)

static int side_buf[QUEUE_MAX];

// ---------- left column by view (1.2.1) ----------
// Albums: albums, Favorites, Recent, playlists. Artists: artists, Favorites, Recent, playlists.
// Folders: ".." (below MUSIC) and the subfolders of the current folder.

static void folder_set(App *a, int f) {
    if (f < 0 || f >= a->folders.count) f = 0;
    a->mnav.folder = f;
    a->fkid_count = folders_children(&a->folders, f, a->fkids, MUSIC_FOLDER_KIDS);
}

// Indexes of the views after the library changed (start, reload, rescan).
// Up one level; the cursor lands on the folder we came from.
static void folder_up(App *a) {
    int from = a->mnav.folder;
    if (from <= 0) return;
    folder_set(a, a->folders.folders[from].parent);
    a->mnav.side_sel = a->mnav.track_sel = 0;
    for (int k = 0; k < a->fkid_count; k++)
        if (a->fkids[k] == from) a->mnav.side_sel = (a->mnav.folder ? 1 : 0) + k;
}

// ---------- .m3u (1.2.1) ----------

static void lower_copy(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = (char)((src[i] >= 'A' && src[i] <= 'Z') ? src[i] + 32 : src[i]);
    dst[i] = 0;
}

// Every .m3u the scan found and Nemo has not seen yet becomes an ordinary (editable) playlist, once.
static void m3u_import(App *a) {
    MusicData *d = a->mdata;
    int imported = 0, changed = 0;
    static int found[QUEUE_MAX];
    for (int i = 0; i < a->mlib.m3u_count; i++) {
        const char *path = a->mlib.m3u[i];
        char id[256];
        lower_copy(id, sizeof id, path);
        if (mplaylist_has(&d->m3u_imported, id)) continue;
        if (d->list_count >= MD_MAX_PLAYLISTS) {
            log_msg("m3u: %s skipped, %d playlists already", path, d->list_count);
            break;
        }
        mplaylist_add(&d->m3u_imported, id);   // also when nothing matched: never retried
        changed = 1;
        int missing = 0, entries = 0;
        char *text = safe_read_text(path, M3U_MAX_BYTES, &missing);
        if (!text) continue;
        int n = m3u_parse(path, text, strlen(text), &a->mlib, found, QUEUE_MAX, &entries);
        free(text);
        log_msg("m3u: %s tracks %d/%d", path, n, entries);
        if (n == 0) continue;
        const char *base = strrchr(path, '/');
        char name[64], try_name[72];
        sjis_to_utf8(base ? base + 1 : path, name, sizeof name);   // card names are Shift-JIS
        char *dot = strrchr(name, '.');
        if (dot) *dot = 0;
        int idx = mdata_playlist_create(d, name);
        for (int k = 2; idx < 0 && k < 100; k++) {   // the name is taken: "Name (2)", "Name (3)"...
            snprintf(try_name, sizeof try_name, "%s (%d)", name, k);
            idx = mdata_playlist_create(d, try_name);
        }
        if (idx < 0) continue;
        for (int k = 0; k < n; k++) mplaylist_add(&d->lists[idx], a->mlib.tracks[found[k]].id);
        imported++;
    }
    if (changed) mdata_save(d, MUSIC_DATA);
    if (imported) {
        char msg[96];
        snprintf(msg, sizeof msg, tr(S_M3U_IMPORTED_FMT), imported);
        ui_toast(a, msg);
    }
}

// The menu's playlist (own, Favorites or Recent) as MUSIC/Playlists/<name>.m3u8 for the PC.
static void m3u_export(App *a) {
    if (a->media_suspended) { ui_toast(a, tr(S_MUSIC_SUSPENDED)); return; }
    const int *tracks;
    int n = music_side_tracks(a, a->mmenu_side, &tracks);
    char safe[64], safe_sjis[128], path[256], shown[160], id[256];
    m3u_safe_name(safe, sizeof safe, music_side_name(a, a->mmenu_side));
    utf8_to_sjis(safe, safe_sjis, sizeof safe_sjis);   // file names on the card are Shift-JIS: right on the PC
    snprintf(path, sizeof path, M3U_DIR "/%s.m3u8", safe_sjis);
    mkdir(M3U_DIR, 0777);
    size_t cap = 64 * 1024;
    char *buf = malloc(cap);
    int len = buf ? m3u_format(buf, cap, path, &a->mlib, tracks, n) : -1;
    int rc = len >= 0 ? safe_write_text(path, buf) : -1;
    free(buf);
    log_msg("m3u: export %s (%d tracks) rc=%d", path, n, rc);
    if (rc != 0) { ui_fail(a, S_M3U_SAVE_FAIL); return; }
    lower_copy(id, sizeof id, path);
    mplaylist_add(&a->mdata->m3u_imported, id);   // our own file must not come back as a duplicate
    mdata_save(a->mdata, MUSIC_DATA);
    char shown_path[200];
    snprintf(shown_path, sizeof shown_path, "MUSIC/Playlists/%s.m3u8", safe);
    snprintf(shown, sizeof shown, tr(S_M3U_SAVED_FMT), shown_path);
    ui_toast(a, shown);
}

static void views_rebuild(App *a) {
    artists_free(&a->artists);
    folders_free(&a->folders);
    if (artists_build(&a->artists, &a->mlib) != 0) log_msg("music: artist index out of memory");
    if (folders_build(&a->folders, &a->mlib, MUSIC_ROOT) != 0) log_msg("music: folder tree out of memory");
    free(a->alltracks);
    int cap = a->mlib.track_count < QUEUE_MAX ? a->mlib.track_count : QUEUE_MAX;
    a->alltracks = cap > 0 ? malloc(sizeof(int) * (size_t)cap) : NULL;
    a->alltrack_count = a->alltracks ? alltracks_build(&a->mlib, a->alltracks, cap) : 0;
    a->mnav.view = a->mdata->view;
    a->mnav.artist_open = -1;
    mnav_clamp_folder(&a->mnav, a->folders.count);
    folder_set(a, a->mnav.folder);
    log_msg("music: views: %d artists, %d folders, %d .m3u", a->artists.count, a->folders.count, a->mlib.m3u_count);
    m3u_import(a);
}

int music_side_count(const App *a) {
    if (a->mnav.view == 2) return (a->mnav.folder ? 1 : 0) + a->fkid_count;
    int first = a->mnav.view == 1 ? a->artists.count : a->mnav.view == 3 ? 1 : a->mlib.album_count;
    return first + 2 + a->mdata->list_count;
}

SideKind music_side_kind(const App *a, int side, int *index) {
    if (a->mnav.view == 2) {
        int up = a->mnav.folder ? 1 : 0;
        if (side < up) { *index = a->folders.count ? a->folders.folders[a->mnav.folder].parent : 0; return SIDE_FOLDER_UP; }
        int k = side - up;
        *index = k >= 0 && k < a->fkid_count ? a->fkids[k] : 0;
        return SIDE_FOLDER;
    }
    int first = a->mnav.view == 1 ? a->artists.count : a->mnav.view == 3 ? 1 : a->mlib.album_count;
    if (side < first) { *index = side; return a->mnav.view == 1 ? SIDE_ARTIST : a->mnav.view == 3 ? SIDE_ALL : SIDE_ALBUM; }
    if (side == first) { *index = 0; return SIDE_FAVORITES; }
    if (side == first + 1) { *index = 0; return SIDE_RECENT; }
    *index = side - first - 2;
    return SIDE_PLAYLIST;
}

int music_artist_rows(const App *a, int artist) {
    return artist >= 0 && artist < a->artists.count ? 1 + a->artists.artists[artist].album_count : 0;
}

int music_folder_all_tracks(const App *a, int folder, const int **tracks) {
    *tracks = side_buf;
    return folders_tracks(&a->folders, &a->mlib, folder, 1, side_buf, QUEUE_MAX);
}

const char *music_side_name(const App *a, int side) {
    int i;
    switch (music_side_kind(a, side, &i)) {
    case SIDE_ALBUM: return a->mlib.albums[i].title;
    case SIDE_ARTIST: return a->artists.artists[i].name[0] ? a->artists.artists[i].name : tr(S_MUSIC_UNKNOWN_ARTIST);
    case SIDE_FOLDER_UP: return "..";
    case SIDE_FOLDER: return folders_name(&a->folders, i);
    case SIDE_ALL: return tr(S_MUSIC_ALL_TRACKS);
    case SIDE_FAVORITES: return tr(S_MUSIC_FAVORITES);
    case SIDE_RECENT: return tr(S_MUSIC_RECENT);
    default: return i < a->mdata->list_count ? a->mdata->lists[i].name : "";
    }
}

static int ids_to_tracks(const App *a, const MPlaylist *p) {
    int n = 0;
    for (int i = 0; i < p->count && n < QUEUE_MAX; i++) {
        int t = musiclib_find(&a->mlib, p->ids[i]);
        if (t >= 0) side_buf[n++] = t;
    }
    return n;
}

int music_side_tracks(const App *a, int side, const int **tracks) {
    *tracks = side_buf;
    if (side < 0 || side >= music_side_count(a)) return 0;
    int i;
    switch (music_side_kind(a, side, &i)) {
    case SIDE_ALBUM: {
        const Album *al = &a->mlib.albums[i];
        int n = al->count < QUEUE_MAX ? al->count : QUEUE_MAX;
        for (int k = 0; k < n; k++) side_buf[k] = al->first + k;
        return n;
    }
    case SIDE_FAVORITES: return ids_to_tracks(a, &a->mdata->favorites);
    case SIDE_RECENT: return ids_to_tracks(a, &a->mdata->recent);
    case SIDE_ARTIST: {                              // overview or "All tracks": everything; else one album
        int open = a->mnav.artist_open;
        if (open <= 0) return artists_tracks(&a->artists, &a->mlib, i, side_buf, QUEUE_MAX);
        const MArtist *ar = &a->artists.artists[i];
        if (open - 1 >= ar->album_count) return 0;
        const Album *al = &a->mlib.albums[a->artists.albums[ar->album_first + open - 1]];
        int n = al->count < QUEUE_MAX ? al->count : QUEUE_MAX;
        for (int k = 0; k < n; k++) side_buf[k] = al->first + k;
        return n;
    }
    case SIDE_FOLDER_UP: return 0;
    case SIDE_FOLDER: return folders_tracks(&a->folders, &a->mlib, i, 0, side_buf, QUEUE_MAX);
    case SIDE_ALL:
        if (a->alltrack_count > 0) memcpy(side_buf, a->alltracks, sizeof(int) * (size_t)a->alltrack_count);
        return a->alltrack_count;
    default: return ids_to_tracks(a, &a->mdata->lists[i]);
    }
}

// ---------- playback ----------

int music_playing(const App *a) {
    (void)a;
    return player_active() && !player_paused();
}

static char *render_state(App *a) {
    uint32_t pos = player_active() ? player_position_ms() : a->music_resume_ms;
    mdata_set_queue(a->mdata, &a->queue, &a->mlib, pos);
    a->music_saved_ms = a->now_ms;
    return mdata_render(a->mdata);
}

void music_save_state(App *a) {
    if (a->music_off) return;                // nothing loaded: would wipe the saved queue
    char *text = render_state(a);
    if (!text || bgsave_now(MUSIC_DATA, text) != 0) log_msg("music: state save failed");
}

// Track switches and periodic saves: the memory stick write runs on the bgsave thread.
static void music_save_state_async(App *a) {
    if (a->music_off) return;
    char *text = render_state(a);
    if (text) bgsave_queue(MUSIC_DATA, text);
}

static int now_playing_visible(const App *a) {
    if (a->nav.tab != TAB_MUSIC) return 0;
    for (int i = 0; i < a->mnav.depth; i++)
        if (a->mnav.stack[i].kind == MS_NOW) return 1;
    return 0;
}

int music_fullscreen(const App *a) {
    return a->nav.tab == TAB_MUSIC && nav_top(&a->nav) == SCR_ROOT && !a->media_suspended &&
           a->mlib.track_count > 0 && now_playing_visible(a);
}

static void announce(App *a, int track) {
    if (now_playing_visible(a)) return;   // the screen already shows the track
    const Track *t = &a->mlib.tracks[track];
    char msg[260];
    snprintf(msg, sizeof msg, "%s — %s", t->title, t->artist[0] ? t->artist : tr(S_MUSIC_UNKNOWN_ARTIST));
    ui_toast(a, msg);
}

static int load_failures;   // consecutive tracks that could not be played (stop after a full lap)

// Ask the loader thread for the current queue track; music_tick starts it when it is ready.
static uint32_t request_ms;   // diagnostics: track switch timing ("stutters on next track" report)

static void play_current(App *a, uint32_t start_ms) {
    int t = queue_current(&a->queue);
    if (t < 0) return;
    request_ms = platform_ms();
    player_request(&a->mlib.tracks[t], start_ms);
}

static void track_started(App *a) {
    int t = queue_current(&a->queue);
    if (t < 0) return;
    load_failures = 0;
    if (!umdmusic_is_track(&a->mlib.tracks[t])) mdata_touch_recent(a->mdata, a->mlib.tracks[t].id);   // a disc comes and goes
    if (power_screen_is_off()) power_set_listening(1, a->mlib.tracks[t].format);   // clock per format
    if (!a->listening) announce(a, t);
    a->music_resume_ms = 0;
    uint32_t t0 = platform_ms();
    music_save_state_async(a);
    log_msg("music: switch timing: load %u ms (request -> ready), save state %u ms",
            (unsigned)(t0 - request_ms), (unsigned)(platform_ms() - t0));
}

static void track_failed(App *a) {
    ui_fail(a, S_MUSIC_ERR_PLAY);
    if (++load_failures >= a->queue.count || queue_next(&a->queue, 1) < 0) {
        load_failures = 0;
        return;
    }
    play_current(a, 0);
}

static void toggle(App *a) {
    if (player_pending()) return;                    // a track is opening: ignore extra presses
    if (player_active()) {
        player_pause(!player_paused());
        music_save_state(a);
    } else if (queue_current(&a->queue) >= 0) {
        play_current(a, a->music_resume_ms);   // restored queue: start where we stopped
    }
}

static void next(App *a) {
    if (queue_next(&a->queue, 1) >= 0) play_current(a, 0);
}

static void prev(App *a) {
    int restart = 0;
    uint32_t pos = player_active() ? player_position_ms() : 0;
    if (queue_prev(&a->queue, pos, &restart) < 0) return;
    if (restart && player_active()) player_seek_ms(0);
    else play_current(a, 0);
}

void music_note(App *a, NoteEvent e) {
    if (a->music_off) return;                // Music is turned off in Settings
    if (a->media_suspended && e != NOTE_NONE) { ui_toast(a, tr(S_MUSIC_SUSPENDED)); return; }
    switch (e) {
    case NOTE_TOGGLE: toggle(a); break;
    case NOTE_NEXT: next(a); break;
    case NOTE_PREV: prev(a); break;
    default: break;
    }
}

void music_hprm(App *a, uint32_t key) {
    if (a->music_off) return;
    uint32_t fresh = key & ~a->hprm_prev;
    a->hprm_prev = key;
    if (a->media_suspended) return;
    if (key & PSP_HPRM_HOLD) return;
    if (fresh & PSP_HPRM_PLAYPAUSE) toggle(a);
    if (fresh & PSP_HPRM_FORWARD) next(a);
    if (fresh & PSP_HPRM_BACK) prev(a);
}

void music_sleep_label(const App *a, char *out, size_t cap) {
    switch (a->sleep.choice) {
    case SLEEP_OFF: snprintf(out, cap, "%s", tr(S_SLEEP_OFF)); break;
    case SLEEP_END_OF_LIST: snprintf(out, cap, "%s", tr(S_SLEEP_END_OF_LIST)); break;
    default:
        if (a->debug && a->sleep.choice == SLEEP_15) { snprintf(out, cap, tr(S_SLEEP_MIN), 1); strncat(out, " (test)", cap - strlen(out) - 1); }
        else snprintf(out, cap, tr(S_SLEEP_MIN), (int)a->sleep.choice * 15);
        break;
    }
}

void music_enter_listening(App *a) {
    a->listening = 1;
    a->listen_started_ms = a->now_ms;
    mnav_push(&a->mnav, MS_LISTEN);
}

static void leave_listening(App *a);

// Everything that must survive sleep happens before it: keep the position, close the track
// (its file handle may not survive), cancel the timer, restore volume and the screen.
static void prepare_for_sleep(App *a) {
    if (player_active()) {
        a->music_resume_ms = player_position_ms();
        player_stop();
    }
    sleep_set(&a->sleep, SLEEP_OFF, 0);
    player_set_fade(100);
    music_save_state(a);
    leave_listening(a);
}

static void leave_listening(App *a) {
    power_set_listening(0, AF_MP3);
    a->listening = 0;
    if (mnav_top(&a->mnav) == MS_LISTEN) mnav_pop(&a->mnav);
}

// Sleep timer / end of list: pause, save, then the normal PSP sleep.
static void go_to_sleep(App *a) {
    log_msg("music: sleep timer -> PSP sleep");
    prepare_for_sleep(a);
    power_sleep();
}

// Rescan of the MUSIC folder on a worker thread (the "Scan" button of the empty Music tab).
static SDL_Thread *rescan_thread;
static MusicLib rescan_lib;
static ScanStats rescan_stats;
static volatile int rescan_done;

static int rescan_main(void *unused) {
    (void)unused;
    music_scan(MUSIC_ROOT, MUSIC_SCAN_CACHE, &rescan_lib, &rescan_stats);
    rescan_done = 1;
    return 0;
}

// UMD Music (1.3.5): the disc's album joins the library once the library is loaded and leaves with the disc.
static void umd_attach(App *a) {
    if (!a->umd_music || a->umd_ref.first_track >= 0 || a->music_off || a->music_lib_loading || a->music_scan_state == 1)
        return;
    if (a->umd_n_streams <= 0) {
        if (a->umd_music_play) { a->umd_music_play = 0; ui_fail(a, S_UMD_MUSIC_EMPTY); }
        return;
    }
    const char *files[64];
    for (int i = 0; i < a->umd_n_streams; i++) files[i] = a->umd_streams[i];
    if (umdmusic_attach_ref(&a->mlib, a->umd_music_title, tr(S_UMD_TRACK_FMT), "disc0:/UMD_AUDIO/STREAM", files,
                            a->umd_n_streams, &a->umd_ref) != 0) {
        log_fail("umdmusic: album not added (out of memory)");
        a->umd_music = 0;
        return;
    }
    log_msg("umdmusic: album \"%s\" added: tracks %d..%d, album %d", a->umd_music_title, a->umd_ref.first_track,
            a->umd_ref.first_track + a->umd_n_streams - 1, a->umd_ref.album);
    views_rebuild(a);
    if (a->umd_music_play) {   // START on the notice: the album from its first track, Now Playing
        a->umd_music_play = 0;
        int tracks[64];
        for (int i = 0; i < a->umd_n_streams; i++) tracks[i] = a->umd_ref.first_track + i;
        queue_set(&a->queue, tracks, a->umd_n_streams, 0);
        play_current(a, 0);
        while (a->nav.depth > 0) nav_pop(&a->nav);
        a->nav.tab = TAB_MUSIC;
        mnav_init(&a->mnav);
        mnav_push(&a->mnav, MS_NOW);
    }
}

int music_current_is_umd(const App *a) {
    int t = queue_current(&a->queue);
    return t >= 0 && t < a->mlib.track_count && umdmusic_is_track(&a->mlib.tracks[t]);
}

void music_umd_detach(App *a) {
    a->umd_music_play = 0;
    if (a->umd_ref.first_track < 0) return;
    int first = a->umd_ref.first_track, removed = 0;
    if (music_current_is_umd(a)) {   // its track was playing: the disc is gone, so is the queue
        player_stop();
        queue_init(&a->queue, (uint32_t)platform_ms() | 1);
        removed = -1;
    } else {
        for (int i = a->queue.count - 1; i >= 0; i--)
            if (a->queue.items[i] >= first && queue_remove(&a->queue, i) == 0) removed++;
    }
    umdmusic_detach(&a->mlib, &a->umd_ref);
    log_msg("umdmusic: album removed (queue %s)", removed < 0 ? "cleared" : "kept");
    if (removed) log_dbg("umdmusic: %d queue items removed", removed);
    views_rebuild(a);
}

// The library replaced or freed: the album went with it (added again by music_tick).
static void umd_forget(App *a) { a->umd_ref.first_track = a->umd_ref.album = -1; }

void music_reload(App *a) {
    if (a->music_off) return;
    musiclib_free(&a->mlib);
    umd_forget(a);
    musiclib_init(&a->mlib);
    ScanStats st = { 0, 0 };
    music_scan(MUSIC_ROOT, MUSIC_SCAN_CACHE, &a->mlib, &st);
    uint32_t pos = 0;
    queue_init(&a->queue, (uint32_t)platform_ms() | 1);
    a->music_resume_ms = mdata_restore_queue(a->mdata, &a->mlib, &a->queue, &pos) == 0 ? pos : 0;
    mnav_init(&a->mnav);
    views_rebuild(a);
}

// Start-up (1.3.1): Home first, then the music library on its thread — after Home's artwork is in
// (the art worker idle for 20 frames) or 6 s after the menu at the latest.
void music_startup_scan(App *a, uint32_t menu_ms) {
    if (a->music_lib_loading != 1 || a->music_scan_state == 1) return;
    static int idle_frames;
    idle_frames = art_busy() ? 0 : idle_frames + 1;
    if (idle_frames < 20 && a->now_ms - menu_ms < 6000) return;
    a->music_lib_loading = 2;
    a->music_lib_t0 = platform_ms();
    music_rescan_start(a);
}

void music_rescan_start(App *a) {
    if (a->music_scan_state == 1) return;
    musiclib_init(&rescan_lib);
    memset(&rescan_stats, 0, sizeof rescan_stats);
    rescan_done = 0;
    a->music_scan_state = 1;
    a->music_scan_seen = 0;
    log_msg("music: rescan of %s started", MUSIC_ROOT);
    rescan_thread = SDL_CreateThread(rescan_main, "nemo_music_scan", NULL);
    if (!rescan_thread) {
        log_msg("music: rescan thread failed: %s", SDL_GetError());
        a->music_scan_state = 2;
    }
}

static void rescan_poll(App *a) {
    if (a->music_scan_state != 1) return;
    a->music_scan_seen = rescan_stats.parsed + rescan_stats.reused;
    if (!rescan_done) return;
    SDL_WaitThread(rescan_thread, NULL);
    rescan_thread = NULL;
    int n = rescan_lib.track_count;
    log_msg("music: rescan found %d tracks, %d albums (parsed %d, cached %d)", n, rescan_lib.album_count,
            rescan_stats.parsed, rescan_stats.reused);
    int card_tracks = a->umd_ref.first_track >= 0 ? a->umd_ref.first_track : a->mlib.track_count;   // a UMD album aside
    if (n > 0 && card_tracks == 0) {   // only offered while the library is empty: no queue to remap
        if (a->umd_ref.first_track >= 0) music_umd_detach(a);
        musiclib_free(&a->mlib);
        umd_forget(a);
        a->mlib = rescan_lib;
        uint32_t pos = 0;
        if (mdata_restore_queue(a->mdata, &a->mlib, &a->queue, &pos) == 0) a->music_resume_ms = pos;
        mnav_init(&a->mnav);
        views_rebuild(a);
        a->music_scan_state = 0;
        if (!a->music_lib_loading) {   // the start-up load is silent
            char msg[96];
            snprintf(msg, sizeof msg, tr(S_MUSIC_SCAN_FOUND_N), n);
            ui_toast(a, msg);
        }
    } else {
        musiclib_free(&rescan_lib);
        a->music_scan_state = n > 0 ? 0 : 2;
    }
    if (a->music_lib_loading) {
        a->music_lib_loading = 0;
        log_time("music_library", platform_ms() - a->music_lib_t0);
    }
}

static void eq_apply(App *a);

void music_init(App *a, int enabled) {
    a->mdata = calloc(1, sizeof *a->mdata);
    a->umd_ref.first_track = a->umd_ref.album = -1;
    mdata_init(a->mdata);
    queue_init(&a->queue, (uint32_t)platform_ms() | 1);
    mnav_init(&a->mnav);
    sleep_set(&a->sleep, SLEEP_OFF, 0);
    uint32_t t0 = platform_ms();
    ScanStats st = { 0, 0 };
    a->music_off = !enabled;
    // 1.3.1: the library is read on a thread once Home is on screen (music_startup_scan); off: not at all.
    a->music_lib_loading = enabled;
    int rc = mdata_load(a->mdata, MUSIC_DATA);
    if (rc < 0) {   // keep the user's file for recovery instead of overwriting it with empty data
        remove(MUSIC_DATA ".bad");
        rename(MUSIC_DATA, MUSIC_DATA ".bad");
        log_msg("music: music.json unreadable, kept as music.json.bad");
    }
    uint32_t pos = 0;
    int restored = mdata_restore_queue(a->mdata, &a->mlib, &a->queue, &pos) == 0;
    a->music_resume_ms = restored ? pos : 0;
    log_msg("music: %d tracks, %d albums in %u ms (parsed %d, cached %d), data rc=%d, queue %s (%d @%u ms)",
            a->mlib.track_count, a->mlib.album_count, (unsigned)(platform_ms() - t0), st.parsed, st.reused, rc,
            restored ? "restored" : "empty", a->queue.count, (unsigned)pos);
    log_time("music_library", platform_ms() - t0);
    bgsave_init();
    if (player_init() != 0) log_msg("music: audio unavailable");
    power_init();
    eq_apply(a);
    views_rebuild(a);
}

void music_set_enabled(App *a, int on) {
    if (on == !a->music_off) return;
    if (!on) {
        music_save_state(a);                 // keep the place for when it comes back
        if (a->listening) leave_listening(a);
        player_stop();
        sleep_set(&a->sleep, SLEEP_OFF, 0);
        a->music_off = 1;
        musiclib_free(&a->mlib);
        umd_forget(a);
        musiclib_init(&a->mlib);
        queue_init(&a->queue, (uint32_t)platform_ms() | 1);
        mnav_init(&a->mnav);
        art_reset();
        hwmp3_shutdown();
        platform_log_memory("music-off");
    } else {
        a->music_off = 0;
        music_reload(a);
        platform_log_memory("music-on");
    }
}

void music_tick(App *a) {
    rescan_poll(a);
    if (power_take_suspended()) {            // power switch / sleep: keep the place
        log_msg("power: suspending (arg 0x%x)", power_last_arg());
        network_on_suspend(a);
        prepare_for_sleep(a);
        power_suspend_done();
    }
    if (power_take_resumed()) {
        log_reopen();                        // the old log handle may not survive sleep
        log_msg("power: resumed (arg 0x%x)", power_last_arg());
        net_on_resume();
        network_on_wake(a);
    }
    if (a->music_off) return;
    umd_attach(a);
    uint32_t poll_t0 = platform_ms();
    int started = player_poll();
    if (started > 0) {
        uint32_t poll_ms = platform_ms() - poll_t0;
        track_started(a);
        log_msg("music: switch timing: start %u ms, total on main thread %u ms", (unsigned)poll_ms,
                (unsigned)(platform_ms() - poll_t0));
    }
    else if (started < 0) track_failed(a);
    if (a->sleep.choice != SLEEP_OFF && a->sleep.choice != SLEEP_END_OF_LIST) {
        player_set_fade(sleep_fade_percent(&a->sleep, a->now_ms));
        if (sleep_due(&a->sleep, a->now_ms)) { go_to_sleep(a); return; }
    }
    if (a->listening && !power_screen_is_off() && a->now_ms - a->listen_started_ms >= LISTEN_DELAY_MS) {
        int t = queue_current(&a->queue);
        power_set_listening(1, t >= 0 ? a->mlib.tracks[t].format : AF_MP3);
    }
    if (player_take_finished()) {
        log_msg("music: finished");
        if (a->now_ms - player_started_ms() < 1000) {   // ended at once: broken file, skip it
            player_stop();
            if (++load_failures < a->queue.count && queue_next(&a->queue, 1) >= 0) play_current(a, 0);
            else load_failures = 0;
            return;
        }
        if (a->sleep.choice == SLEEP_END_OF_LIST && queue_at_last(&a->queue)) {
            player_stop();
            go_to_sleep(a);
            return;
        }
        if (queue_next(&a->queue, 0) >= 0) {
            play_current(a, 0);
        } else {
            player_stop();               // end of the list: stay on the last track
            a->music_resume_ms = 0;
            music_save_state(a);
        }
    }
    if (music_playing(a) && a->now_ms - a->music_saved_ms >= SAVE_EVERY_MS) music_save_state_async(a);
    static uint32_t last_log;
    if (music_playing(a) && a->now_ms - last_log >= 60000) {
        last_log = a->now_ms;
        int t = queue_current(&a->queue);
        power_log_minute(t >= 0 ? a->mlib.tracks[t].format : AF_UNKNOWN, (unsigned)player_underruns());
    }
}

// ---------- equalizer (1.2.1) ----------

static void eq_apply(App *a) {
    int g[EQ_BANDS];
    eq_preset_gains((EqPreset)a->mdata->eq_preset, a->mdata->eq_custom, g);
    player_set_eq(g, a->mdata->eq_on);
    power_set_eq(a->mdata->eq_on && !eq_is_flat(g));
}

// Row 0 on/off, 1 preset, 2.. bands (moving a band switches to "Custom", starting from the preset's values).
static void eq_change(App *a, int row, int dir) {
    MusicData *d = a->mdata;
    if (row == 0) {
        d->eq_on = !d->eq_on;
    } else if (row == 1) {
        d->eq_preset = (d->eq_preset + dir + EQP_COUNT) % EQP_COUNT;
    } else if (row - 2 < EQ_BANDS) {
        if (d->eq_preset != EQP_CUSTOM) {
            eq_preset_gains((EqPreset)d->eq_preset, d->eq_custom, d->eq_custom);
            d->eq_preset = EQP_CUSTOM;
        }
        int *g = &d->eq_custom[row - 2];
        *g += dir;
        if (*g > EQ_MAX_DB) *g = EQ_MAX_DB;
        if (*g < -EQ_MAX_DB) *g = -EQ_MAX_DB;
    }
    eq_apply(a);
    mdata_save(d, MUSIC_DATA);
}

// ---------- menus ----------

#define PICK_SIDE (-2)   // mpick_track: the picker adds the menu's whole left-column entry

// Tracks a left-column menu acts on: folders with their subfolders, anything else as listed.
static int menu_side_tracks(App *a, const int **tracks) {
    int f;
    if (music_side_kind(a, a->mmenu_side, &f) == SIDE_FOLDER) return music_folder_all_tracks(a, f, tracks);
    return music_side_tracks(a, a->mmenu_side, tracks);
}

static void open_menu(App *a, int ctx, int index) {
    a->mmenu_ctx = ctx;
    a->mmenu_count = 0;
    a->mmenu_track = -1;
    a->mmenu_side = a->mnav.side_sel;
    const int *tracks;
    int n = music_side_tracks(a, a->mnav.side_sel, &tracks);
    if (ctx == 2) {                                  // Now Playing
        a->mmenu_track = queue_current(&a->queue);
        if (a->mmenu_track >= 0) {
            a->mmenu[a->mmenu_count++] = MM_TO_PLAYLIST;
            a->mmenu[a->mmenu_count++] = MM_FAVORITE;
        }
        a->mmenu[a->mmenu_count++] = MM_QUEUE;
        a->mmenu[a->mmenu_count++] = MM_SLEEP;
        a->mmenu[a->mmenu_count++] = MM_EQ;
        a->mmenu[a->mmenu_count++] = MM_LISTEN;
    } else if (ctx == 1) {                           // track in the library
        if (index < 0 || index >= n) { mnav_pop(&a->mnav); return; }
        a->mmenu_track = tracks[index];
        a->mmenu[a->mmenu_count++] = MM_TO_QUEUE;
        a->mmenu[a->mmenu_count++] = MM_TO_PLAYLIST;
        a->mmenu[a->mmenu_count++] = MM_FAVORITE;
    } else if (a->mnav.view == 2) {                  // folder: play / queue / playlist, with subfolders
        int f;
        if (music_side_kind(a, a->mnav.side_sel, &f) == SIDE_FOLDER) {
            if (n > 0) a->mmenu[a->mmenu_count++] = MM_PLAY_FOLDER;
            a->mmenu[a->mmenu_count++] = MM_PLAY_FOLDER_ALL;
            a->mmenu[a->mmenu_count++] = MM_SHUFFLE_FOLDER_ALL;
            a->mmenu[a->mmenu_count++] = MM_TO_QUEUE;
            a->mmenu[a->mmenu_count++] = MM_TO_PLAYLIST;
        }
    } else {                                         // album / artist / playlist entry
        int i;
        if (n > 0) a->mmenu[a->mmenu_count++] = MM_TO_QUEUE;
        SideKind k = music_side_kind(a, a->mnav.side_sel, &i);
        if (k == SIDE_PLAYLIST || k == SIDE_FAVORITES || k == SIDE_RECENT || k == SIDE_ALL)
            a->mmenu[a->mmenu_count++] = MM_EXPORT_M3U;
        if (k == SIDE_PLAYLIST) {
            a->mmenu[a->mmenu_count++] = MM_RENAME_PLAYLIST;
            a->mmenu[a->mmenu_count++] = MM_DELETE_PLAYLIST;
        }
    }
    if (a->mmenu_count == 0) mnav_pop(&a->mnav);
}

static void add_to_queue(App *a, const int *tracks, int n) {
    int was_empty = a->queue.count == 0;
    for (int i = 0; i < n; i++)
        if (queue_append(&a->queue, tracks[i]) != 0) break;
    ui_toast(a, tr(S_MUSIC_QUEUED));
    if (was_empty) a->music_resume_ms = 0;
    music_save_state(a);
}

static void open_osk(App *a, OskPurpose purpose, const char *initial) {
    a->osk_purpose = purpose;
    osk_init(&a->osk, initial);
    mnav_push(&a->mnav, MS_OSK);
}

static void menu_pick(App *a, int index, int dir) {
    if (index < 0 || index >= a->mmenu_count) return;
    MusicMenuItem item = a->mmenu[index];
    if (item == MM_SLEEP) {                          // ←/→ (or ✕) change the value in place
        sleep_set(&a->sleep, sleep_step(a->sleep.choice, dir ? dir : 1), a->now_ms);
        if (a->debug && a->sleep.choice == SLEEP_15) a->sleep.deadline_ms = a->now_ms + 60000;   // test timer
        player_set_fade(100);
        log_msg("music: sleep timer %d (debug=%d)", (int)a->sleep.choice, a->debug);
        return;
    }
    if (dir != 0) return;                            // other rows ignore ←/→
    const char *track_id = a->mmenu_track >= 0 ? a->mlib.tracks[a->mmenu_track].id : NULL;
    switch (item) {
    case MM_TO_QUEUE:
        mnav_pop(&a->mnav);
        if (a->mmenu_ctx == 1) {
            add_to_queue(a, &a->mmenu_track, 1);
        } else {
            const int *tracks;
            int n = menu_side_tracks(a, &tracks);
            add_to_queue(a, tracks, n);
        }
        break;
    case MM_TO_PLAYLIST:
        mnav_pop(&a->mnav);
        a->mpick_track = a->mmenu_ctx == 0 ? PICK_SIDE : a->mmenu_track;
        mnav_push(&a->mnav, MS_PICK);
        break;
    case MM_PLAY_FOLDER: case MM_PLAY_FOLDER_ALL: case MM_SHUFFLE_FOLDER_ALL: {
        mnav_pop(&a->mnav);
        const int *tracks;
        int f, n;
        music_side_kind(a, a->mmenu_side, &f);
        if (item == MM_PLAY_FOLDER) n = music_side_tracks(a, a->mmenu_side, &tracks);
        else n = music_folder_all_tracks(a, f, &tracks);
        if (n <= 0) break;
        queue_set(&a->queue, tracks, n, 0);
        if (item == MM_SHUFFLE_FOLDER_ALL) queue_set_shuffle(&a->queue, 1);
        play_current(a, 0);
        mnav_push(&a->mnav, MS_NOW);
        break;
    }
    case MM_FAVORITE:
        if (!track_id) break;
        if (mplaylist_has(&a->mdata->favorites, track_id)) mplaylist_remove(&a->mdata->favorites, track_id);
        else mplaylist_add(&a->mdata->favorites, track_id);
        mdata_save(a->mdata, MUSIC_DATA);
        break;
    case MM_QUEUE:
        mnav_pop(&a->mnav);
        mnav_push(&a->mnav, MS_QUEUE);
        break;
    case MM_LISTEN:
        mnav_pop(&a->mnav);
        music_enter_listening(a);
        break;
    case MM_RENAME_PLAYLIST: {
        int i;
        mnav_pop(&a->mnav);
        music_side_kind(a, a->mmenu_side, &i);
        open_osk(a, OSK_FOR_RENAME_PLAYLIST, a->mdata->lists[i].name);
        break;
    }
    case MM_DELETE_PLAYLIST:
        mnav_pop(&a->mnav);
        a->mconfirm = 0;
        mnav_push(&a->mnav, MS_CONFIRM);
        break;
    case MM_EQ:
        mnav_pop(&a->mnav);
        mnav_push(&a->mnav, MS_EQ);
        break;
    case MM_EXPORT_M3U:
        mnav_pop(&a->mnav);
        m3u_export(a);
        break;
    default: break;
    }
}

// Tracks the picker adds: one track, or the whole left-column entry (folders: with subfolders).
static int pick_tracks(App *a, const int **tracks) {
    if (a->mpick_track == PICK_SIDE) return menu_side_tracks(a, tracks);
    *tracks = &a->mpick_track;
    return a->mpick_track >= 0 ? 1 : 0;
}

static void playlist_pick(App *a, int index) {
    if (a->mpick_track < 0 && a->mpick_track != PICK_SIDE) return;
    if (index == a->mdata->list_count) {             // "New playlist…"
        open_osk(a, OSK_FOR_NEW_PLAYLIST, "");
        return;
    }
    MPlaylist *p = &a->mdata->lists[index];
    if (a->mpick_track == PICK_SIDE) {               // a whole album/folder: add (never toggles off)
        const int *tracks;
        int n = pick_tracks(a, &tracks);
        for (int i = 0; i < n; i++) mplaylist_add(p, a->mlib.tracks[tracks[i]].id);
        ui_toast(a, tr(S_MUSIC_QUEUED_PLAYLIST));
        mnav_pop(&a->mnav);
    } else {
        const char *id = a->mlib.tracks[a->mpick_track].id;
        if (mplaylist_has(p, id)) mplaylist_remove(p, id);
        else mplaylist_add(p, id);
    }
    mdata_save(a->mdata, MUSIC_DATA);
}

static void osk_input(App *a, Action act) {
    OskState st = osk_handle(&a->osk, act);
    if (st == OSK_CANCEL) { mnav_pop(&a->mnav); return; }
    if (st != OSK_DONE) return;
    int ok;
    if (a->osk_purpose == OSK_FOR_RENAME_PLAYLIST) {
        int i;
        music_side_kind(a, a->mmenu_side, &i);
        ok = mdata_playlist_rename(a->mdata, i, a->osk.text) == 0;
    } else {
        int idx = mdata_playlist_create(a->mdata, a->osk.text);
        ok = idx >= 0;
        if (ok) {
            const int *tracks;
            int n = pick_tracks(a, &tracks);
            for (int i = 0; i < n; i++) mplaylist_add(&a->mdata->lists[idx], a->mlib.tracks[tracks[i]].id);
        }
    }
    if (!ok) { ui_toast(a, tr(S_ERR_FOLDER_NAME)); return; }
    mdata_save(a->mdata, MUSIC_DATA);
    mnav_pop(&a->mnav);
}

void music_action(App *a, Action act) {
    if (a->media_suspended) {
        if (act == ACT_CONFIRM) ui_toast(a, tr(S_MUSIC_SUSPENDED));
        return;
    }
    if (a->mlib.track_count == 0) {          // empty library: the only action is the scan button
        if (act == ACT_CONFIRM) music_rescan_start(a);
        return;
    }
    MusicScreen top = mnav_top(&a->mnav);
    if (top == MS_OSK) { osk_input(a, act); return; }
    if (top == MS_LISTEN) {                          // L/R switch tracks in the dark (1.3.11, as SELECT+L/R do);
        if (act == ACT_TAB_PREV) { prev(a); return; }  // any other button wakes the screen and does nothing else
        if (act == ACT_TAB_NEXT) { next(a); return; }
        leave_listening(a);
        return;
    }
    const int *tracks;
    int n = music_side_tracks(a, a->mnav.side_sel, &tracks);
    int side_idx;
    SideKind side_kind = music_side_kind(a, a->mnav.side_sel, &side_idx);
    a->mmodel.side_count = music_side_count(a);
    // Artist overview: the right column lists "All tracks" + albums, not tracks.
    a->mmodel.track_count = side_kind == SIDE_ARTIST && a->mnav.artist_open < 0 ? music_artist_rows(a, side_idx) : n;
    a->mmodel.folder_is_root = a->mnav.folder == 0;
    a->mmodel.menu_count = a->mmenu_count;
    a->mmodel.queue_count = a->queue.count > a->queue.pos ? a->queue.count - a->queue.pos : 0;
    a->mmodel.pick_count = a->mdata->list_count + 1;
    a->mmodel.eq_rows = 2 + EQ_BANDS;
    MusicCmd c = mnav_handle(&a->mnav, act, &a->mmodel);
    switch (c.type) {
    case MC_PLAY_TRACK:
        queue_set(&a->queue, tracks, n, c.index);
        play_current(a, 0);
        break;
    case MC_OPEN_MENU: open_menu(a, c.dir, c.index); break;
    case MC_MENU_PICK: menu_pick(a, c.index, c.dir); break;
    case MC_TOGGLE_PAUSE: toggle(a); break;
    case MC_SEEK:
        if (player_active()) {
            int32_t pos = (int32_t)player_position_ms() + c.dir * SEEK_STEP_MS;
            uint32_t dur = player_duration_ms();
            if (pos < 0) pos = 0;
            if (dur && (uint32_t)pos > dur) pos = (int32_t)dur;
            player_seek_ms((uint32_t)pos);
        }
        break;
    case MC_SHUFFLE:
        queue_set_shuffle(&a->queue, !a->queue.shuffle);
        music_save_state(a);
        break;
    case MC_REPEAT:
        queue_cycle_repeat(&a->queue);
        music_save_state(a);
        break;
    case MC_NP_BUTTON:
        switch (c.index) {
        case NP_SHUFFLE: queue_set_shuffle(&a->queue, !a->queue.shuffle); music_save_state(a); break;
        case NP_PREV: prev(a); break;
        case NP_PLAY: toggle(a); break;
        case NP_NEXT: next(a); break;
        case NP_REPEAT: queue_cycle_repeat(&a->queue); music_save_state(a); break;
        default: break;
        }
        break;
    case MC_QUEUE_JUMP:
        if (queue_jump(&a->queue, a->queue.pos + c.index) >= 0) play_current(a, 0);
        break;
    case MC_QUEUE_REMOVE:
        if (c.index > 0) queue_remove(&a->queue, a->queue.pos + c.index);
        music_save_state(a);
        break;
    case MC_PICK: playlist_pick(a, c.index); break;
    case MC_EQ_CHANGE: eq_change(a, c.index, c.dir); break;
    case MC_VIEW_NEXT:
        mnav_set_view(&a->mnav, (a->mnav.view + 1) % 4);   // albums, artists, folders, all tracks
        folder_set(a, 0);
        a->mdata->view = a->mnav.view;
        mdata_save(a->mdata, MUSIC_DATA);
        break;
    case MC_SIDE_CHANGED: a->mnav.artist_open = -1; break;
    case MC_ARTIST_OPEN:
        a->mnav.artist_open = c.index;
        a->mnav.track_sel = 0;
        break;
    case MC_ARTIST_BACK:
        a->mnav.track_sel = a->mnav.artist_open;
        a->mnav.artist_open = -1;
        break;
    case MC_FOLDER_ENTER:
        if (side_kind == SIDE_FOLDER_UP) {
            folder_up(a);
        } else if (side_kind == SIDE_FOLDER) {
            int kids[1];
            if (folders_children(&a->folders, side_idx, kids, 1) > 0) {
                folder_set(a, side_idx);
                a->mnav.side_sel = a->mnav.track_sel = 0;
            } else {
                a->mnav.column = 1;              // a leaf folder: straight to its tracks
            }
        }
        break;
    case MC_FOLDER_UP: folder_up(a); break;
    case MC_EQ_RESET:
        a->mconfirm = 1;
        mnav_push(&a->mnav, MS_CONFIRM);
        break;
    case MC_CONFIRM_YES: {
        int i;
        if (a->mconfirm == 1) {
            for (int b = 0; b < EQ_BANDS; b++) a->mdata->eq_custom[b] = 0;
            a->mdata->eq_preset = EQP_FLAT;
            a->mconfirm = 0;
            eq_apply(a);
            mdata_save(a->mdata, MUSIC_DATA);
        } else if (music_side_kind(a, a->mmenu_side, &i) == SIDE_PLAYLIST) {
            log_msg("music: delete playlist \"%s\"", a->mdata->lists[i].name);
            mdata_playlist_delete(a->mdata, i);
            mdata_save(a->mdata, MUSIC_DATA);
            a->mnav.side_sel = 0;
        }
        break;
    }
    default: break;
    }
}
