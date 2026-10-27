#ifndef NEMO_SELECTKEY_H
#define NEMO_SELECTKEY_H

#include <stdint.h>

typedef struct {
    int held;   // SELECT is down
    int used;   // a combo happened during this hold
} SelectKey;

typedef enum { SELK_NONE, SELK_TAP } SelectEvent;

// SELECT as a modifier. combo != 0 marks this hold as used (SELECT+L/R, SELECT+START, SELECT+TRIANGLE).
// SELK_TAP once when SELECT is released and no combo happened during the hold. buttons = currently held bits.
SelectEvent selectkey_update(SelectKey *k, uint32_t buttons, int combo);

#endif
