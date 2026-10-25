#include "input.h"
#include "buttons.h"

#define STICK_DEADZONE   48
#define REPEAT_DELAY_MS  300
#define REPEAT_RATE_MS   80

int input_actions(uint32_t pressed, int confirm_is_cross, Action *out, int max) {
    static const struct { uint32_t bit; Action act; } map[] = {
        { NB_UP, ACT_UP }, { NB_DOWN, ACT_DOWN }, { NB_LEFT, ACT_LEFT }, { NB_RIGHT, ACT_RIGHT },
        { NB_TRIANGLE, ACT_MENU }, { NB_SQUARE, ACT_QUICK },
        { NB_LTRIGGER, ACT_TAB_PREV }, { NB_RTRIGGER, ACT_TAB_NEXT },
        { NB_START, ACT_START }, { NB_HOME, ACT_HOME }, { NB_NOTE, ACT_NOTE },
    };
    int n = 0;
    uint32_t confirm = confirm_is_cross ? NB_CROSS : NB_CIRCLE;
    uint32_t back = confirm_is_cross ? NB_CIRCLE : NB_CROSS;
    if ((pressed & confirm) && n < max) out[n++] = ACT_CONFIRM;
    if ((pressed & back) && n < max) out[n++] = ACT_BACK;
    for (unsigned i = 0; i < sizeof map / sizeof *map && n < max; i++)
        if (pressed & map[i].bit) out[n++] = map[i].act;
    return n;
}

uint32_t input_stick_dirs(uint8_t lx, uint8_t ly) {
    uint32_t d = 0;
    if (lx < 128 - STICK_DEADZONE) d |= NB_LEFT;
    if (lx > 128 + STICK_DEADZONE) d |= NB_RIGHT;
    if (ly < 128 - STICK_DEADZONE) d |= NB_UP;
    if (ly > 128 + STICK_DEADZONE) d |= NB_DOWN;
    return d;
}

uint32_t input_repeat(InputRepeat *r, uint32_t held_dirs, uint32_t now_ms) {
    uint32_t fresh = held_dirs & ~r->held;
    r->held = held_dirs;
    if (!held_dirs) return 0;
    if (fresh) {
        r->next_ms = now_ms + REPEAT_DELAY_MS;
        return fresh;
    }
    if ((int32_t)(now_ms - r->next_ms) >= 0) {
        r->next_ms = now_ms + REPEAT_RATE_MS;
        return held_dirs;
    }
    return 0;
}

Glyph input_glyph(Action a, int confirm_is_cross) {
    switch (a) {
    case ACT_CONFIRM: return confirm_is_cross ? GLYPH_CROSS : GLYPH_CIRCLE;
    case ACT_BACK: return confirm_is_cross ? GLYPH_CIRCLE : GLYPH_CROSS;
    case ACT_MENU: return GLYPH_TRIANGLE;
    case ACT_QUICK: return GLYPH_SQUARE;
    case ACT_TAB_PREV: return GLYPH_L;
    case ACT_TAB_NEXT: return GLYPH_R;
    case ACT_START: return GLYPH_START;
    case ACT_SELECT: return GLYPH_SELECT;
    case ACT_NOTE: return GLYPH_NOTE;
    case ACT_UP: case ACT_DOWN: case ACT_LEFT: case ACT_RIGHT: return GLYPH_DPAD;
    default: return GLYPH_NONE;
    }
}
