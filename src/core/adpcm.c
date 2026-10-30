#include "adpcm.h"

static const int STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767 };
static const int INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int pred, index; } Ch;

static int16_t step(Ch *c, int nib) {
    int d = ((2 * (nib & 7) + 1) * STEP[c->index]) >> 3;   // the exact form (as ffmpeg), not the shifted sum
    c->pred += (nib & 8) ? -d : d;
    if (c->pred > 32767) c->pred = 32767;
    if (c->pred < -32768) c->pred = -32768;
    c->index += INDEX[nib];
    if (c->index < 0) c->index = 0;
    if (c->index > 88) c->index = 88;
    return (int16_t)c->pred;
}

int adpcm_block_frames(int block_align, int channels) {
    if (channels < 1 || channels > 2 || block_align < 4 * channels) return 0;
    return (block_align - 4 * channels) * 2 / channels + 1;
}

int adpcm_ima_block(const uint8_t *blk, int size, int channels, int16_t *out, int max_frames) {
    int frames = adpcm_block_frames(size, channels);
    if (!frames || frames > max_frames) return 0;
    Ch ch[2];
    for (int c = 0; c < channels; c++) {   // the header: the first sample and the step index
        ch[c].pred = (int16_t)(blk[4 * c] | blk[4 * c + 1] << 8);
        ch[c].index = blk[4 * c + 2] > 88 ? 88 : blk[4 * c + 2];
        out[c] = (int16_t)ch[c].pred;
    }
    const uint8_t *p = blk + 4 * channels;
    int n = 1;
    // Groups of 4 bytes per channel (8 samples each), channels in turn.
    while (n + 8 <= frames) {
        for (int c = 0; c < channels; c++) {
            for (int b = 0; b < 4; b++) {
                uint8_t v = p[c * 4 + b];
                out[(n + 2 * b) * channels + c] = step(&ch[c], v & 15);
                out[(n + 2 * b + 1) * channels + c] = step(&ch[c], v >> 4);
            }
        }
        p += 4 * channels;
        n += 8;
    }
    return n;
}
