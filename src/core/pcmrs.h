#ifndef NEMO_PCMRS_H
#define NEMO_PCMRS_H

#include <stdint.h>

// Video sound (1.3.3): any PCM of a camera AVI (16-bit, 8-bit, mu-law; mono or stereo; any rate) to the
// PSP's 44100 Hz stereo, with linear interpolation.
typedef enum { PCMF_S16, PCMF_U8, PCMF_MULAW } PcmFormat;

typedef struct {
    int rate, channels;
    PcmFormat fmt;
    uint32_t step;      // input frames per output frame, 16.16
    uint32_t frac;      // position between in[0] and in[1], 16.16
} PcmRs;

void pcmrs_init(PcmRs *r, int rate, int channels, PcmFormat fmt);
// in: in_frames frames of the source format. Writes up to out_frames stereo frames to out; *used = input
// frames fully consumed (the rest is given again next time). Returns the frames written.
int  pcmrs_run(PcmRs *r, const void *in, int in_frames, int *used, int16_t *out, int out_frames);

#endif
