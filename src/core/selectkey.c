#include "selectkey.h"
#include "buttons.h"

SelectEvent selectkey_update(SelectKey *k, uint32_t buttons, int combo) {
    if (buttons & NB_SELECT) {
        if (!k->held) {
            k->held = 1;
            k->used = 0;
        }
        if (combo) k->used = 1;
        return SELK_NONE;
    }
    if (!k->held) return SELK_NONE;
    k->held = 0;
    return k->used ? SELK_NONE : SELK_TAP;
}
