#ifndef NEMO_FLACSRC_H
#define NEMO_FLACSRC_H

#include <stdint.h>

// FLAC file -> interleaved int16 stereo at 44100 Hz (1.2.0). SDL_mixer in pspdev is built without FLAC,
// so dr_flac decodes; mono is doubled, other rates are resampled linearly. Streams from the file:
// only one block and a small buffer live in memory.
#define FLACSRC_RATE 44100

typedef struct FlacSrc FlacSrc;

FlacSrc *flacsrc_open(const char *path, uint32_t start_ms);   // NULL: not FLAC / >2 channels / >48 kHz / >24 bit
uint32_t flacsrc_read(FlacSrc *s, int16_t *dst, uint32_t frames);   // frames written; 0 = end of stream
uint32_t flacsrc_duration_ms(const FlacSrc *s);                     // 0 if unknown
void     flacsrc_close(FlacSrc *s);

#endif
