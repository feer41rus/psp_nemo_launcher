#ifndef NEMO_INPUT_H
#define NEMO_INPUT_H

#include <stdint.h>

typedef enum {
    ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_CONFIRM, ACT_BACK,
    ACT_MENU, ACT_QUICK, ACT_TAB_PREV, ACT_TAB_NEXT, ACT_START, ACT_HOME, ACT_NOTE,
    ACT_SELECT   // hints only: SELECT is a modifier and never produced by input_actions
} Action;

typedef enum {
    GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_L, GLYPH_R,
    GLYPH_START, GLYPH_NOTE, GLYPH_DPAD, GLYPH_SELECT, GLYPH_NONE
} Glyph;

// Translate newly pressed buttons (NB_*) into actions. confirm_is_cross: system button_assign == 1.
int input_actions(uint32_t pressed, int confirm_is_cross, Action *out, int max);

// Analog stick -> NB_LEFT/RIGHT/UP/DOWN bits (deadzone 48 around the 128 center).
uint32_t input_stick_dirs(uint8_t lx, uint8_t ly);

// Auto-repeat for held directions: fires on press, then after 300 ms, then every 80 ms.
typedef struct {
    uint32_t held;
    uint32_t next_ms;
} InputRepeat;
uint32_t input_repeat(InputRepeat *r, uint32_t held_dirs, uint32_t now_ms);

// Which physical button to draw for an action.
Glyph input_glyph(Action a, int confirm_is_cross);

#endif
