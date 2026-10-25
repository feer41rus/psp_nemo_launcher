#include "notekey.h"
#include "buttons.h"

NoteEvent notekey_update(NoteKey *k, uint32_t buttons, uint32_t *pressed) {
    NoteEvent e = NOTE_NONE;
    if (buttons & NB_NOTE) {
        if (!k->held) {
            k->held = 1;
            k->used = 0;
        }
        if (*pressed & NB_RTRIGGER) { e = NOTE_NEXT; k->used = 1; }
        else if (*pressed & NB_LTRIGGER) { e = NOTE_PREV; k->used = 1; }
        *pressed &= ~(uint32_t)(NB_NOTE | NB_LTRIGGER | NB_RTRIGGER);
    } else if (buttons & NB_SELECT) {   // PSP Go / E1000 have no ♪: SELECT+L / SELECT+R
        if (*pressed & NB_RTRIGGER) e = NOTE_NEXT;
        else if (*pressed & NB_LTRIGGER) e = NOTE_PREV;
        *pressed &= ~(uint32_t)(NB_LTRIGGER | NB_RTRIGGER);
        if (k->held) {               // ♪ released while SELECT is down: still a ♪ tap
            k->held = 0;
            if (!k->used && e == NOTE_NONE) e = NOTE_TOGGLE;
        }
    } else if (k->held) {
        k->held = 0;
        if (!k->used) e = NOTE_TOGGLE;
    }
    return e;
}
