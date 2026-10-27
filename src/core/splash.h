#ifndef NEMO_SPLASH_H
#define NEMO_SPLASH_H

#include <stdint.h>

// Start-up splash screens (1.2.3): "Welcome!" + slogan (with the chime), then "thank you for buying from
// NCL"; each can be off in Settings › System › Nemo. Shown only at console power-on, while Nemo loads in the
// background; a new button press skips the current one.
typedef enum { SPL_HELLO, SPL_NCL, SPL_WAIT_LOAD, SPL_DONE } SplashPhase;

#define SPLASH_HELLO_MS 2600
#define SPLASH_NCL_MS 2200
#define SPLASH_FADE_MS 300
#define SPLASH_SLOGAN_DELAY_MS 500
#define SPLASH_BAR_FROM_MS 500     // the light bar grows with the chime
#define SPLASH_BAR_TO_MS 1300

typedef struct {
    SplashPhase phase;
    uint32_t phase_ms;   // when the current phase began
    int hello_on, ncl_on;
    int armed;           // buttons were released since the phase began: the next press skips
} Splash;

void        splash_init(Splash *s, int hello_on, int ncl_on, uint32_t now_ms);   // both off -> SPL_DONE
SplashPhase splash_update(Splash *s, uint32_t now_ms, int buttons_down, int loaded);

// Per frame: alphas 0..255 and the bar width 0..100 %. HELLO: title, slogan (later), bar, logo.
// NCL: title = the thank-you line, slogan = the NCL logo (later).
typedef struct { int title_a, slogan_a, bar_w_pct, logo_a; } SplashFx;
void splash_fx(const Splash *s, uint32_t now_ms, SplashFx *fx);

#endif
