#ifndef NEMO_HWMP3_H
#define NEMO_HWMP3_H

#include <stdint.h>

// MP3 through the PSP's hardware decoder (sceMp3 on the Media Engine). Decoded PCM goes to
// SDL_mixer through Mix_HookMusic, so volume, mixing and the audio device stay SDL's.
// Opening reads only the header (no whole-file scan), so a track switch is near instant.
typedef struct HwTrack HwTrack;

int  hwmp3_init(void);                                   // load the AV modules once; 0 ok
void hwmp3_shutdown(void);                               // unload them (Music turned off in Settings)
// Loader thread: header, decoder handle, first ~0.2 s decoded. NULL = use SDL_mixer instead.
HwTrack *hwmp3_open(const char *path, uint32_t start_ms);
void hwmp3_discard(HwTrack *t);                          // an opened track that will not play
void hwmp3_start(HwTrack *t);                            // main thread: becomes the playing track
void hwmp3_stop(void);                                   // stop and release the playing track
int  hwmp3_active(void);
void hwmp3_pause(int paused);
uint32_t hwmp3_position_ms(void);
uint32_t hwmp3_duration_ms(void);
void hwmp3_set_volume(int percent);                      // 0..100, applied in the hook
int  hwmp3_take_finished(void);                          // 1 once when the playing track ended
uint32_t hwmp3_starved(void);                            // hook calls that found too little PCM

#endif
