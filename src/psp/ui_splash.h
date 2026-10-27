#ifndef NEMO_UI_SPLASH_H
#define NEMO_UI_SPLASH_H

#include "library.h"
#include "splash.h"

// Start-up splash screens on the PSP (1.2.3), drawn over black like the stock Sony logo.
// Run: at least one is on in Settings and this start is a power-on (no "away" marker from a game / XMB /
// ARK); the marker is removed either way.
int  splash_should_run(const NemoSettings *s);
void splash_draw(const Splash *s, uint32_t now_ms);
void splash_draw_wait(void);     // after the last splash while loading still runs: no app state read
void splash_free(void);          // textures, once the menu takes over

#endif
