#ifndef NEMO_UISND_H
#define NEMO_UISND_H

#include <stdint.h>
#include "input.h"
#include "synth.h"

// Which interface sound an action makes (1.2.3). Silent while music plays (author), with the screen off,
// during a battery test or a splash screen. Held arrows: ticks at most ~12/s, edge thuds at most 4/s.
typedef struct { uint32_t last_move_ms, last_edge_ms, last_any_ms; int any; } UiSndState;
// quiet: battery test, splash; settings: the cursor is in the Settings list (its own move sound)
typedef struct { int enabled, music_playing, screen_off, quiet, settings; } UiSndGate;

void    uisnd_init(UiSndState *st);
// moved: the focus changed (arrows); 0 = the cursor hit an edge.
UiSound uisnd_pick(UiSndState *st, const UiSndGate *g, Action act, int moved, uint32_t now_ms);
// A toast: a short double tick, unless another sound has just played (no double sound after ✕).
UiSound uisnd_toast(UiSndState *st, const UiSndGate *g, uint32_t now_ms);

#endif
