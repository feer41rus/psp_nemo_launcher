#include "sfx.h"
#include "log.h"
#include "player.h"
#include "power.h"
#include "ui.h"
#include <SDL2/SDL_mixer.h>
#include <stdlib.h>

#define CH_CLICK 0
#define CH_CHIME 1
#define VOL_CLICK 48
#define VOL_CHIME 80

static Mix_Chunk *chunks[UIS_COUNT];
static int16_t *bufs[UIS_COUNT];
static int ready;

// Mono synth samples doubled into the mixer's stereo S16 format.
static Mix_Chunk *make_chunk(UiSound s, int16_t **buf) {
    int n = synth_length(s);
    int16_t *mono = malloc(sizeof(int16_t) * (size_t)n), *st = malloc(sizeof(int16_t) * 2 * (size_t)n);
    if (!mono || !st) { free(mono); free(st); return NULL; }
    synth_render(s, mono, n);
    for (int i = 0; i < n; i++) st[2 * i] = st[2 * i + 1] = mono[i];
    free(mono);
    Mix_Chunk *c = Mix_QuickLoad_RAW((Uint8 *)st, (Uint32)(sizeof(int16_t) * 2 * (size_t)n));
    if (!c) { free(st); return NULL; }
    Mix_VolumeChunk(c, s == UIS_CHIME ? VOL_CHIME : VOL_CLICK);
    *buf = st;
    return c;
}

int sfx_init(void) {
    if (ready) return 0;
    if (player_audio_open() != 0) return -1;
    int freq = 0, ch = 0;
    Uint16 fmt = 0;
    Mix_QuerySpec(&freq, &fmt, &ch);
    if (freq != SYNTH_RATE || fmt != AUDIO_S16SYS || ch != 2) {
        log_msg("sfx: mixer %d Hz fmt 0x%x ch %d, sounds off", freq, fmt, ch);
        return -1;
    }
    Mix_AllocateChannels(2);
    for (int s = UIS_MOVE; s < UIS_COUNT; s++) chunks[s] = make_chunk((UiSound)s, &bufs[s]);
    ready = 1;
    log_msg("sfx: ready");
    return 0;
}

void sfx_play(UiSound s) {
    if (!ready || s <= UIS_NONE || s >= UIS_COUNT || !chunks[s]) return;
    Mix_PlayChannel(s == UIS_CHIME ? CH_CHIME : CH_CLICK, chunks[s], 0);
}

int sfx_free_chime(void) {
    if (!chunks[UIS_CHIME]) return 1;
    if (Mix_Playing(CH_CHIME)) return 0;
    Mix_FreeChunk(chunks[UIS_CHIME]);
    free(bufs[UIS_CHIME]);
    chunks[UIS_CHIME] = NULL;
    bufs[UIS_CHIME] = NULL;
    return 1;
}

UiSndGate sfx_gate(const App *a) {
    UiSndGate g = { a->lib && a->lib->settings.ui_sounds, music_playing(a), power_screen_is_off(),
                    battery_running(a) || a->splash_active,
                    a->nav.tab == TAB_SETTINGS && nav_top(&a->nav) == SCR_ROOT };
    return g;
}
