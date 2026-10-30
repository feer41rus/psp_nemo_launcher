#ifndef NEMO_BIGROT_H
#define NEMO_BIGROT_H

#include <stdint.h>

// Rotating a photo too large for memory (1.3.1): bands of rows are rotated in memory and laid out in
// a raw file of the rotated picture on the card, which is then streamed out row by row. 3 bytes a pixel.
typedef struct { void *ctx; int (*read_row)(void *ctx, uint8_t *row); } BrSource;          // top to bottom; 0 / -1
typedef struct { void *ctx; int (*write_row)(void *ctx, const uint8_t *row); } BrSink;     // result rows; 0 / -1
typedef struct { int percent; volatile int cancel; } BrProgress;   // percent: 0-80 bands, 80-100 output

// The result is h wide and w high (cw = 1 clockwise). raw_path is always removed.
// 0 = done, -1 = error, -2 = cancelled (checked after each band and every 64 output rows).
int bigrot_rotate(BrSource *src, int w, int h, int cw, const char *raw_path, int band_rows, BrSink *dst, BrProgress *pr);
long long bigrot_need_bytes(int w, int h, long long src_size);   // card space: raw file + max(old size, w*h)

#endif
