#include "player.h"
#include "hwmp3.h"
#include "flacplay.h"
#include "umdsrc.h"
#include "log.h"
#include "platform.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <string.h>
#include <strings.h>

#define AUDIO_RATE 44100
#define AUDIO_CHUNK 4096          // ~93 ms per mixer callback: more slack for memory-stick contention
#define GAP_MS 250                // a callback later than this counts as an underrun

static Mix_Music *music;
static int opened;
static volatile int finished;
static volatile uint32_t underruns;
static volatile uint32_t last_mix_ms;
static int paused;
static int fade = 100;
static uint32_t started_ms;
static EqState eq;                       // equalizer, applied after mixing (all three paths) (1.2.1)

// Loader thread: Mix_LoadMUS scans the whole file, so it must not run on the UI thread.
static SDL_Thread *loader;
static SDL_sem *load_sem;
static SDL_mutex *load_lock;
static char want_path[256];
static uint32_t want_start_ms;
static volatile uint32_t want_gen;        // bumped by every request
static Mix_Music *ready_music;            // result handed to the main thread
static uint32_t ready_gen, ready_start_ms;
static volatile int ready;
static char ready_title[128];
static char want_title[128];
// Hardware MP3 path (hwmp3.c): the loader opens it instead of Mix_LoadMUS.
static int hw;                            // the current track plays through hwmp3
static HwTrack *ready_hw;
static int want_seek, ready_seek;         // a reopen of the current track at a new position (hw seek)
static char ready_path[256], cur_path[256], cur_title[128];
static int seek_pending;
// FLAC path (flacplay.c, 1.2.0): SDL_mixer here has no FLAC.
static int fl;                            // the current track plays through flacplay
static FlacTrack *ready_fl;
static uint32_t seek_target_ms;

static int is_mp3(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && strcasecmp(dot, ".mp3") == 0;
}

static int is_flac(const char *path) {
    const char *dot = strrchr(path, '.');
    return (dot && strcasecmp(dot, ".flac") == 0) || umdsrc_is_path(path);   // UMD Music: the same PCM path (1.3.5)
}

static void on_finished(void) { finished = 1; }

// Runs on the audio thread after every mixed chunk: measures gaps between chunks.
static void post_mix(void *udata, Uint8 *stream, int len) {
    (void)udata;
    if (eq.active) eq_process(&eq, (int16_t *)stream, len / 4);
    uint32_t now = platform_ms();
    int playing = (Mix_PlayingMusic() && !Mix_PausedMusic()) || ((hw || fl) && !paused);
    if (last_mix_ms && playing && now - last_mix_ms > GAP_MS) underruns++;
    last_mix_ms = now;
}

static int loader_main(void *unused) {
    (void)unused;
    uint32_t done_gen = 0;
    for (;;) {
        SDL_SemWait(load_sem);
        // The main thread takes the previous result first: sceMp3 has only two handles
        // (the playing track + this one).
        while (ready) SDL_Delay(5);
        char path[256], title[128];
        uint32_t gen, start;
        int seek;
        SDL_LockMutex(load_lock);
        gen = want_gen;
        start = want_start_ms;
        seek = want_seek;
        memcpy(path, want_path, sizeof path);
        memcpy(title, want_title, sizeof title);
        SDL_UnlockMutex(load_lock);
        if (gen == done_gen) continue;           // a later post for a request we already served
        Mix_Music *m = NULL;
        FlacTrack *f = is_flac(path) ? flacplay_open(path, start) : NULL;
        HwTrack *h = is_mp3(path) && hwmp3_init() == 0 ? hwmp3_open(path, start) : NULL;
        if (is_flac(path)) {
            if (!f) log_msg("music: flac unsupported %s", path);
        } else if (!h) {
            if (is_mp3(path)) log_msg("music: hw fallback to SDL_mixer: %s", path);
            m = Mix_LoadMUS(path);
            if (!m) log_msg("music: load failed %s: %s", path, Mix_GetError());
        }
        SDL_LockMutex(load_lock);
        ready_music = m;
        ready_hw = h;
        ready_fl = f;
        ready_seek = seek;
        memcpy(ready_path, path, sizeof ready_path);
        ready_gen = gen;
        ready_start_ms = start;
        memcpy(ready_title, title, sizeof ready_title);
        ready = 1;
        SDL_UnlockMutex(load_lock);
        done_gen = gen;
    }
    return 0;
}

static int open_audio(void) {
    if (Mix_OpenAudio(AUDIO_RATE, AUDIO_S16SYS, 2, AUDIO_CHUNK) != 0) {
        log_msg("player: Mix_OpenAudio failed: %s", Mix_GetError());
        return -1;
    }
    Mix_HookMusicFinished(on_finished);
    Mix_SetPostMix(post_mix, NULL);
    Mix_VolumeMusic(MIX_MAX_VOLUME * fade / 100);
    opened = 1;
    return 0;
}

int player_audio_open(void) {
    if (opened) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_msg("player: SDL audio init failed: %s", SDL_GetError());
        return -1;
    }
    return open_audio();
}

int player_init(void) {
    if (player_audio_open() != 0) return -1;
    int got = Mix_Init(MIX_INIT_MP3 | MIX_INIT_FLAC | MIX_INIT_OGG);
    log_msg("player: Mix_Init formats 0x%x", got);
    load_sem = SDL_CreateSemaphore(0);
    load_lock = SDL_CreateMutex();
    loader = SDL_CreateThread(loader_main, "nemo_music_load", NULL);
    if (!loader) log_msg("player: loader thread failed: %s", SDL_GetError());
    return 0;
}

void player_stop(void) {
    if (hw) {
        hwmp3_stop();
        hw = 0;
    }
    if (fl) {
        flacplay_stop();
        fl = 0;
    }
    if (music) {
        Mix_HaltMusic();
        Mix_FreeMusic(music);
        music = NULL;
    }
    finished = 0;
    paused = 0;
}

static void request(const char *path, const char *title, uint32_t start_ms, int seek) {
    SDL_LockMutex(load_lock);
    strncpy(want_path, path, sizeof want_path - 1);
    want_path[sizeof want_path - 1] = '\0';
    strncpy(want_title, title, sizeof want_title - 1);
    want_title[sizeof want_title - 1] = '\0';
    want_start_ms = start_ms;
    want_seek = seek;
    want_gen++;
    SDL_UnlockMutex(load_lock);
    SDL_SemPost(load_sem);
}

void player_request(const Track *t, uint32_t start_ms) {
    seek_pending = 0;
    request(t->path, t->title, start_ms, 0);
}

int player_pending(void) { return ready || (loader && want_gen != ready_gen); }

int player_poll(void) {
    if (!ready) return 0;
    SDL_LockMutex(load_lock);
    Mix_Music *m = ready_music;
    HwTrack *h = ready_hw;
    FlacTrack *f = ready_fl;
    int seek = ready_seek;
    uint32_t gen = ready_gen, start = ready_start_ms;
    char title[128], path[256];
    memcpy(title, ready_title, sizeof title);
    memcpy(path, ready_path, sizeof path);
    ready_music = NULL;
    ready_hw = NULL;
    ready_fl = NULL;
    ready = 0;
    int stale = gen != want_gen;
    SDL_UnlockMutex(load_lock);
    if (stale) {                                 // superseded by a newer request
        if (m) Mix_FreeMusic(m);
        hwmp3_discard(h);
        flacplay_discard(f);
        return 0;
    }
    if (seek) seek_pending = 0;
    if (!m && !h && !f) return seek ? 0 : -1;
    if (!opened && open_audio() != 0) {
        if (m) Mix_FreeMusic(m);
        hwmp3_discard(h);
        flacplay_discard(f);
        return -1;
    }
    memcpy(cur_path, path, sizeof cur_path);
    memcpy(cur_title, title, sizeof cur_title);
    if (f) {
        if (music) {                             // an SDL_mixer track ends here
            Mix_HaltMusic();
            Mix_FreeMusic(music);
            music = NULL;
        }
        if (hw) {
            hwmp3_stop();
            hw = 0;
        }
        flacplay_set_volume(fade);
        flacplay_start(f);                       // releases the previous FLAC track
        fl = 1;
        finished = 0;
        paused = 0;
        last_mix_ms = 0;
        if (!seek) started_ms = platform_ms();
        log_msg("music: play \"%s\" from %u ms (duration %u ms) path=flac%s", title, (unsigned)start,
                (unsigned)flacplay_duration_ms(), seek ? " (seek)" : "");
        return seek ? 0 : 1;
    }
    if (h) {
        if (music) {                             // an SDL_mixer track ends here
            Mix_HaltMusic();
            Mix_FreeMusic(music);
            music = NULL;
        }
        if (fl) {
            flacplay_stop();
            fl = 0;
        }
        hwmp3_set_volume(fade);
        hwmp3_start(h);                          // releases the previous hw track
        hw = 1;
        finished = 0;
        paused = 0;
        last_mix_ms = 0;
        if (!seek) started_ms = platform_ms();
        log_msg("music: play \"%s\" from %u ms (duration %u ms) path=hw%s", title, (unsigned)start,
                (unsigned)hwmp3_duration_ms(), seek ? " (seek)" : "");
        return seek ? 0 : 1;
    }
    player_stop();
    music = m;
    // Position is applied as playback starts (no blip from 0:00 when resuming).
    // From the start: no seek (seeking to 0 cost 1-2.6 s on the main thread for some MP3s).
    int rc = start ? Mix_FadeInMusicPos(music, 0, 0, start / 1000.0) : Mix_PlayMusic(music, 0);
    if (rc != 0) {
        log_msg("music: play failed: %s", Mix_GetError());
        Mix_FreeMusic(music);
        music = NULL;
        return -1;
    }
    last_mix_ms = 0;
    started_ms = platform_ms();
    log_msg("music: play \"%s\" from %u ms (duration %u ms) path=sdl", title, (unsigned)start,
            (unsigned)player_duration_ms());
    return seek ? 0 : 1;
}

void player_pause(int p) {
    if (hw) hwmp3_pause(p);
    else if (fl) flacplay_pause(p);
    else if (!music) return;
    else if (p) Mix_PauseMusic();
    else Mix_ResumeMusic();
    paused = p;
    last_mix_ms = 0;
}

int player_paused(void) { return (music || hw || fl) && paused; }
int player_active(void) { return music != NULL || hw || fl; }
uint32_t player_started_ms(void) { return started_ms; }

uint32_t player_position_ms(void) {
    if (seek_pending) return seek_target_ms;     // hw seek: the reopen is on its way
    if (hw) return hwmp3_position_ms();
    if (fl) return flacplay_position_ms();
    if (!music) return 0;
    double s = Mix_GetMusicPosition(music);
    return s > 0 ? (uint32_t)(s * 1000.0) : 0;
}

uint32_t player_duration_ms(void) {
    if (hw) return hwmp3_duration_ms();
    if (fl) return flacplay_duration_ms();
    if (!music) return 0;
    double s = Mix_MusicDuration(music);
    return s > 0 ? (uint32_t)(s * 1000.0) : 0;
}

void player_seek_ms(uint32_t ms) {
    if (hw || fl) {                              // reopen at the new place (header + TOC / FLAC seek table)
        uint32_t dur = hw ? hwmp3_duration_ms() : flacplay_duration_ms();
        if (dur > 2000 && ms > dur - 1000) ms = dur - 1000;   // past the end restarted the track before
        seek_target_ms = ms;
        seek_pending = 1;
        request(cur_path, cur_title, ms, 1);
    } else if (music) {
        Mix_SetMusicPosition(ms / 1000.0);
    }
}

void player_set_fade(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == fade) return;
    fade = percent;
    Mix_VolumeMusic(MIX_MAX_VOLUME * fade / 100);
    hwmp3_set_volume(fade);
    flacplay_set_volume(fade);
}

int player_take_finished(void) {
    if (hw) return hwmp3_take_finished();
    if (fl) return flacplay_take_finished();
    if (!finished) return 0;
    finished = 0;
    return 1;
}

void player_set_eq(const int gains[EQ_BANDS], int on) {
    static const int flat[EQ_BANDS] = { 0 };
    SDL_LockAudio();                             // the mixer callback reads eq
    eq_design(&eq, on ? gains : flat, AUDIO_RATE);
    SDL_UnlockAudio();
}

uint32_t player_underruns(void) { return underruns + hwmp3_starved() + flacplay_starved(); }
