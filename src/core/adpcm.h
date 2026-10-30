#ifndef NEMO_ADPCM_H
#define NEMO_ADPCM_H

#include <stdint.h>

// IMA ADPCM as cameras write it into AVI (WAV format 0x11), for the video player (1.3.3).
int adpcm_block_frames(int block_align, int channels);   // frames one block gives
// One block into interleaved 16-bit frames; returns the frames written (0 for a broken block).
int adpcm_ima_block(const uint8_t *blk, int size, int channels, int16_t *out, int max_frames);

#endif
