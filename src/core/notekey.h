#ifndef NEMO_NOTEKEY_H
#define NEMO_NOTEKEY_H

#include <stdint.h>

typedef enum { NOTE_NONE, NOTE_TOGGLE, NOTE_NEXT, NOTE_PREV } NoteEvent;

typedef struct {
    int held;   // ♪ is down
    int used;   // a combo happened during this hold
} NoteKey;

// ♪ tap = toggle (on release), ♪+R = next, ♪+L = previous; SELECT+R / SELECT+L do the same. While ♪ is held, NB_NOTE and the
// triggers are removed from *pressed so they do not switch tabs. buttons = currently held bits.
NoteEvent notekey_update(NoteKey *k, uint32_t buttons, uint32_t *pressed);

#endif
