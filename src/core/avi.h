#ifndef NEMO_AVI_H
#define NEMO_AVI_H

#include <stdint.h>

// AVI reading for camera video (1.3.3): Motion JPEG frames and PCM sound, from idx1 or by walking movi.
typedef int (*AviReadFn)(void *ctx, long long off, void *buf, int n);   // bytes read, -1 error

typedef struct { long long offset; int size; } AviChunk;   // payload position and size

typedef struct {
    char vcodec[5];                // "MJPG"
    int width, height;
    uint32_t us_per_frame;
    AviChunk *video;
    int n_video;
    int has_audio;
    int a_format, a_channels, a_rate, a_bits;   // WAVEFORMATEX: 1 = PCM, 7 = mu-law, 0x11 = IMA ADPCM
    int a_block_align;         // bytes per block (ADPCM)
    AviChunk *audio;
    int n_audio;
    long long audio_bytes;
} Avi;

int  avi_open(Avi *a, AviReadFn read, void *ctx, long long file_size);   // 0 ok, -1 not an AVI / broken
void avi_close(Avi *a);
int64_t avi_duration_ms(const Avi *a);
int  avi_frame_at(const Avi *a, int64_t ms);
long long avi_audio_at(const Avi *a, int64_t ms);   // byte position in the sound stream

#endif
