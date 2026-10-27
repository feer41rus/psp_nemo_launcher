#ifndef NEMO_FLACPLAY_H
#define NEMO_FLACPLAY_H

#include <stdint.h>

// FLAC playback (1.2.0): SDL_mixer in pspdev has no FLAC, so flacsrc (dr_flac) decodes on its own
// thread into a small PCM ring that Mix_HookMusic plays — the same shape as hwmp3.
typedef struct FlacTrack FlacTrack;

// Loader thread: open, check the format, decode the first ~0.2 s. NULL = unsupported or unreadable.
FlacTrack *flacplay_open(const char *path, uint32_t start_ms);
void flacplay_discard(FlacTrack *t);       // an opened track that will not play
void flacplay_start(FlacTrack *t);         // main thread: becomes the playing track (hook installed)
void flacplay_stop(void);                  // remove the hook and release the playing track
int  flacplay_active(void);
void flacplay_pause(int paused);
uint32_t flacplay_position_ms(void);
uint32_t flacplay_duration_ms(void);
void flacplay_set_volume(int percent);     // 0..100, applied in the hook
int  flacplay_take_finished(void);         // 1 once when the playing track ended
uint32_t flacplay_starved(void);           // hook calls that found too little PCM

#endif
