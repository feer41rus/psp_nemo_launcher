#ifndef NEMO_EQ_H
#define NEMO_EQ_H

#include <stdint.h>

// 5-band music equalizer (1.2.1): RBJ biquads on 16-bit stereo, one for all music. 60 Hz is a low shelf,
// 14 kHz a high shelf, the rest peaks (Q 1). A pre-gain of -max(boost) keeps loud tracks from clipping.
#define EQ_BANDS 5
#define EQ_MAX_DB 12

typedef enum { EQP_FLAT, EQP_BASS, EQP_ROCK, EQP_POP, EQP_JAZZ, EQP_CLASSIC, EQP_VOCAL, EQP_CUSTOM, EQP_COUNT } EqPreset;

extern const int EQ_FREQS[EQ_BANDS];   // Hz

void eq_preset_gains(EqPreset p, const int custom[EQ_BANDS], int out[EQ_BANDS]);   // EQP_CUSTOM -> custom
int  eq_is_flat(const int gains[EQ_BANDS]);

typedef struct { float b0, b1, b2, a1, a2; } EqBiquad;
typedef struct {
    EqBiquad bq[EQ_BANDS];
    int on[EQ_BANDS];          // band gain != 0 (others are skipped)
    float pre;
    int active;                // any band != 0
    float z[EQ_BANDS][2][2];   // filter state per band and channel
} EqState;

void eq_design(EqState *s, const int gains[EQ_BANDS], int rate);   // keeps the filter state z
void eq_process(EqState *s, int16_t *pcm, int frames);             // interleaved stereo, in place, saturating

#endif
