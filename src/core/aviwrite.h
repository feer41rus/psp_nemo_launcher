#ifndef NEMO_AVIWRITE_H
#define NEMO_AVIWRITE_H

#include <stdint.h>
#include <stdio.h>

// Camera video (1.3.6): an AVI of Motion JPEG frames (as the camera gives them) and 16-bit PCM sound,
// with an idx1 index; sizes are patched in on close. Single-threaded: the caller serialises.
typedef struct { char id[4]; uint32_t offset, size; } AviwIdx;

typedef struct {
    FILE *f;
    int fps, rate, channels;
    uint32_t n_video, audio_bytes, max_frame;
    long movi_pos;        // the 'movi' fourcc
    long pos;             // the end of the data written
    AviwIdx *idx;
    int n_idx, cap_idx;
    uint8_t *last;        // the previous frame, for a repeat
    uint32_t last_size, last_cap;
    int failed;
} AviWriter;

int  aviw_open(AviWriter *w, const char *path, int width, int height, int fps, int audio_rate, int audio_channels);
int  aviw_video(AviWriter *w, const void *jpeg, uint32_t size);   // NULL/0: the previous frame again
int  aviw_audio(AviWriter *w, const void *pcm, uint32_t bytes);   // -1 without a sound stream
long aviw_size(const AviWriter *w);                                // bytes so far (AVI 1.0: keep under 2 GB)
int  aviw_close(AviWriter *w);                                     // 0 = a complete file

#endif
