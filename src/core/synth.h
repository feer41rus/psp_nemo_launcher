#ifndef NEMO_SYNTH_H
#define NEMO_SYNTH_H

#include <stdint.h>

// Nemo's own interface sounds (1.2.3): quiet XMB-like clicks and the welcome chime, synthesized at start
// (no sample files, no licences). Mono signed 16-bit at SYNTH_RATE.
#define SYNTH_RATE 44100

typedef enum { UIS_NONE, UIS_MOVE, UIS_CONFIRM, UIS_BACK, UIS_TAB, UIS_EDGE, UIS_MENU, UIS_CHIME,
               UIS_SET_MOVE,   // moving in Settings: lower and softer than UIS_MOVE (author, 1.2.3)
               UIS_COUNT } UiSound;

int synth_length(UiSound s);                          // samples; 0 for UIS_NONE
int synth_render(UiSound s, int16_t *out, int cap);   // writes min(cap, length) samples, returns that

#endif
