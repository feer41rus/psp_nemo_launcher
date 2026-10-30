#include "flacplay.h"
#include "flacsrc.h"
#include "umdsrc.h"
#include "pcmring.h"
#include "log.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdlib.h>

#define RING_FRAMES 32768     // ~0.74 s at 44.1 kHz, 128 KB: enough slack for memory-stick stalls (PSP-1000 RAM)
#define CHUNK_FRAMES 2048
#define PREFILL_FRAMES 8820   // ~0.2 s ready before the switch
#define UMD_RING_FRAMES 131072   // ~3 s: a UMD seek or spin-up stalls far longer than a card (1.3.5)

// The PCM source: FLAC from the card, or a UMD Music stream (1.3.5) — both 44.1 kHz stereo.
static uint32_t src_read(FlacTrack *t, int16_t *out, uint32_t n);

struct FlacTrack {
    FlacSrc *src;
    UmdSrc *umd;
    PcmRing ring;
    SDL_Thread *thread;
    volatile int quit, dec_done, ended;
    uint32_t start_ms, duration_ms;
    volatile uint32_t played;   // frames handed to the mixer
    int16_t chunk[2 * CHUNK_FRAMES];
};

static FlacTrack *cur;
static volatile int paused, finished;
static volatile int volume = 100;
static volatile uint32_t starved;

// Decode one chunk into the ring; 0 at the end of the stream.
static int decode_chunk(FlacTrack *t) {
    uint32_t n = src_read(t, t->chunk, CHUNK_FRAMES);
    if (n == 0) return 0;
    pcmring_write(&t->ring, t->chunk, n);
    return 1;
}

static uint32_t src_read(FlacTrack *t, int16_t *out, uint32_t n) {
    return t->umd ? umdsrc_read(t->umd, out, n) : flacsrc_read(t->src, out, n);
}

static int decode_thread(void *arg) {
    FlacTrack *t = arg;
    while (!t->quit && !t->dec_done) {
        if (t->ring.cap_frames - pcmring_fill(&t->ring) < CHUNK_FRAMES) {
            SDL_Delay(10);
            continue;
        }
        if (!decode_chunk(t)) t->dec_done = 1;
    }
    return 0;
}

static void release(FlacTrack *t) {
    if (!t) return;
    if (t->thread) {
        t->quit = 1;
        SDL_WaitThread(t->thread, NULL);
    }
    flacsrc_close(t->src);
    umdsrc_close(t->umd);
    pcmring_free(&t->ring);
    free(t);
}

FlacTrack *flacplay_open(const char *path, uint32_t start_ms) {
    FlacTrack *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    int umd = umdsrc_is_path(path);
    if (umd) t->umd = umdsrc_open(path, start_ms);
    else t->src = flacsrc_open(path, start_ms);
    if ((!t->src && !t->umd) || pcmring_init(&t->ring, umd ? UMD_RING_FRAMES : RING_FRAMES) != 0) {
        log_msg("flac: unsupported or unreadable: %s", path);
        release(t);
        return NULL;
    }
    t->start_ms = start_ms;
    t->duration_ms = umd ? umdsrc_duration_ms(t->umd) : flacsrc_duration_ms(t->src);
    while (pcmring_fill(&t->ring) < PREFILL_FRAMES)
        if (!decode_chunk(t)) { t->dec_done = 1; break; }
    if (!t->dec_done) {
        t->thread = SDL_CreateThread(decode_thread, "nemo_flac", t);
        if (!t->thread) {
            log_msg("flac: thread failed: %s", path);
            release(t);
            return NULL;
        }
    }
    return t;
}

void flacplay_discard(FlacTrack *t) { release(t); }

// SDL_mixer audio thread: the stream was cleared to silence before this call.
static void hook(void *udata, Uint8 *stream, int len) {
    FlacTrack *t = udata;
    if (!t || paused) return;
    uint32_t want = (uint32_t)len / 4;
    uint32_t n = pcmring_read(&t->ring, (int16_t *)stream, want);
    if (volume < 100) {
        Sint16 *s = (Sint16 *)stream;
        for (uint32_t i = 0; i < n * 2; i++) s[i] = (Sint16)(s[i] * volume / 100);
    }
    t->played += n;
    if (n < want) {
        if (t->dec_done && pcmring_fill(&t->ring) == 0) {
            if (!t->ended) {                     // once per track
                t->ended = 1;
                finished = 1;
            }
        } else {
            starved++;
        }
    }
}

void flacplay_start(FlacTrack *t) {
    FlacTrack *old = cur;
    paused = 0;
    finished = 0;
    Mix_HookMusic(hook, t);                      // locks the mixer's device: the old track is out after this
    cur = t;
    if (old && old != t) release(old);
}

void flacplay_stop(void) {
    FlacTrack *old = cur;
    if (!old) return;
    Mix_HookMusic(NULL, NULL);
    cur = NULL;
    release(old);
    finished = 0;
}

int flacplay_active(void) { return cur != NULL; }
void flacplay_pause(int p) { paused = p; }
uint32_t flacplay_position_ms(void) {
    FlacTrack *t = cur;
    return t ? t->start_ms + (uint32_t)((uint64_t)t->played * 1000 / FLACSRC_RATE) : 0;
}
uint32_t flacplay_duration_ms(void) { return cur ? cur->duration_ms : 0; }
void flacplay_set_volume(int percent) { volume = percent; }
int flacplay_take_finished(void) {
    if (!finished || !cur) return 0;
    finished = 0;
    return 1;
}
uint32_t flacplay_starved(void) { return starved; }
