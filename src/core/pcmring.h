#ifndef NEMO_PCMRING_H
#define NEMO_PCMRING_H

#include <stdint.h>

// Single producer / single consumer ring of interleaved int16 stereo frames: the FLAC decoder thread
// writes, the audio callback reads (1.2.0). wr/rd only grow; fill = wr - rd.
typedef struct {
    int16_t *buf;
    uint32_t cap_frames;
    volatile uint32_t wr, rd;
} PcmRing;

int      pcmring_init(PcmRing *r, uint32_t cap_frames);   // 0, -1 out of memory
void     pcmring_free(PcmRing *r);
uint32_t pcmring_write(PcmRing *r, const int16_t *src, uint32_t frames);   // frames written (may be fewer)
uint32_t pcmring_read(PcmRing *r, int16_t *dst, uint32_t frames);          // frames read (may be fewer)
uint32_t pcmring_fill(const PcmRing *r);
void     pcmring_clear(PcmRing *r);   // only while neither side runs

#endif
