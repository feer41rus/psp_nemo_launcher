#ifndef NEMO_PLAYER_H
#define NEMO_PLAYER_H

#include <stdint.h>
#include "music.h"
#include "eq.h"

// Music playback engine (SDL2_mixer). One track at a time, streamed from the memory stick.
// Opening a track (which scans the whole MP3) runs on a loader thread: player_request() queues it,
// player_poll() starts it on the main thread once it is ready.
int  player_init(void);
int  player_audio_open(void);            // SDL audio + mixer open (idempotent); interface sounds use it too
void player_request(const Track *t, uint32_t start_ms);   // newest request wins
int  player_poll(void);                  // 1 = requested track started, -1 = it failed, 0 = nothing new
int  player_pending(void);               // a request is being loaded
void player_stop(void);
void player_pause(int paused);
int  player_paused(void);
int  player_active(void);                // a track is loaded (playing or paused)
uint32_t player_position_ms(void);
uint32_t player_duration_ms(void);       // 0 if unknown
uint32_t player_started_ms(void);        // platform_ms() when the current track started
void player_seek_ms(uint32_t ms);
void player_set_fade(int percent);       // music volume 0..100 (sleep timer fade)
int  player_take_finished(void);         // 1 once after a track played to its end
uint32_t player_underruns(void);
// Equalizer for all music (1.2.1): gains in dB per band; on = 0 or all-zero gains bypass it.
void player_set_eq(const int gains[EQ_BANDS], int on);         // audio gaps > 150 ms observed by the mixer callback

#endif
