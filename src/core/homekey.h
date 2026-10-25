#ifndef NEMO_HOMEKEY_H
#define NEMO_HOMEKEY_H

#include <stdint.h>

// HOME: a tap acts on release (go to Home), holding it opens the power window.
#define HOME_HOLD_MS 1500       // author: 3 s felt too long
#define HOME_BAR_DELAY_MS 300

typedef enum { HOME_NONE, HOME_TAP, HOME_HOLD } HomeEvent;

typedef struct {
    int held;           // a fresh press is being tracked
    int fired;          // HOME_HOLD already sent during this press
    uint32_t down_ms;
} HomeKey;

// Removes NB_HOME from *pressed. Starts only on a fresh press (NB_HOME in *pressed), so a HOME
// that was already down (e.g. it woke the screen) does nothing. HOME_HOLD once after HOME_HOLD_MS;
// HOME_TAP on release when HOLD did not fire.
HomeEvent homekey_update(HomeKey *k, uint32_t buttons, uint32_t *pressed, uint32_t now_ms);
// Hold bar fill 0..100; 0 before HOME_BAR_DELAY_MS, when not held or after HOLD fired.
int homekey_progress(const HomeKey *k, uint32_t now_ms);

#endif
