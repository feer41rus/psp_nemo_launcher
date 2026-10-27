#ifndef NEMO_SFX_H
#define NEMO_SFX_H

#include "synth.h"
#include "uisnd.h"

#include "ui.h"

// Interface sounds on the PSP (1.2.3): the synth.c sounds as SDL_mixer chunks. Channel 0: clicks (a new
// one cuts the old one), channel 1: the welcome chime.
int  sfx_init(void);                 // opens audio if needed; 0 ok
void sfx_play(UiSound s);            // UIS_NONE or before sfx_init: nothing
int  sfx_free_chime(void);           // frees the chime (~200 KB) once it is not playing; 1 = freed
UiSndGate sfx_gate(const App *a);

#endif
