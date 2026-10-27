#include "splash.h"
#include <string.h>

static void enter(Splash *s, SplashPhase p, uint32_t now_ms) {
    s->phase = p;
    s->phase_ms = now_ms;
    s->armed = 0;
}

// The phase after `p` that is switched on; past the last one: wait for loading or done.
static SplashPhase next_phase(const Splash *s, SplashPhase p, int loaded) {
    if (p == SPL_HELLO && s->ncl_on) return SPL_NCL;
    return loaded ? SPL_DONE : SPL_WAIT_LOAD;
}

void splash_init(Splash *s, int hello_on, int ncl_on, uint32_t now_ms) {
    memset(s, 0, sizeof *s);
    s->hello_on = hello_on;
    s->ncl_on = ncl_on;
    enter(s, hello_on ? SPL_HELLO : ncl_on ? SPL_NCL : SPL_DONE, now_ms);
}

SplashPhase splash_update(Splash *s, uint32_t now_ms, int buttons_down, int loaded) {
    if (s->phase == SPL_DONE) return SPL_DONE;
    if (s->phase == SPL_WAIT_LOAD) {
        if (loaded) enter(s, SPL_DONE, now_ms);
        return s->phase;
    }
    uint32_t len = s->phase == SPL_HELLO ? SPLASH_HELLO_MS : SPLASH_NCL_MS;
    int skip = s->armed && buttons_down;
    if (!buttons_down) s->armed = 1;
    if (skip || now_ms - s->phase_ms >= len) enter(s, next_phase(s, s->phase, loaded), now_ms);
    return s->phase;
}

static int ramp(uint32_t t, uint32_t from, uint32_t span, int top) {
    if (t <= from) return 0;
    if (t >= from + span) return top;
    return (int)((t - from) * (uint32_t)top / span);
}

void splash_fx(const Splash *s, uint32_t now_ms, SplashFx *fx) {
    memset(fx, 0, sizeof *fx);
    if (s->phase != SPL_HELLO && s->phase != SPL_NCL) return;
    uint32_t t = now_ms - s->phase_ms, len = s->phase == SPL_HELLO ? SPLASH_HELLO_MS : SPLASH_NCL_MS;
    fx->title_a = ramp(t, 0, SPLASH_FADE_MS, 255);
    fx->slogan_a = ramp(t, SPLASH_SLOGAN_DELAY_MS, SPLASH_FADE_MS, 255);
    fx->bar_w_pct = s->phase == SPL_HELLO ? ramp(t, SPLASH_BAR_FROM_MS, SPLASH_BAR_TO_MS - SPLASH_BAR_FROM_MS, 100) : 0;
    if (t + SPLASH_FADE_MS > len) {   // the last moments: everything fades out together
        uint32_t left = t < len ? len - t : 0;
        fx->title_a = fx->title_a * (int)left / SPLASH_FADE_MS;
        fx->slogan_a = fx->slogan_a * (int)left / SPLASH_FADE_MS;
    }
    fx->logo_a = fx->title_a;
}
