#include "batcells.h"

void batcells(int pct, int charging, int blink_on, BatCells *out) {
    out->solid = 0;
    out->blink = -1;
    out->solid_col = BC_TEXT;
    out->blink_col = BC_GREY;
    if (pct < 0) return;
    if (pct > 100) pct = 100;
    if (!charging) {
        out->solid = (pct + 24) / 25;      // a started cell counts: 1 % shows one cell
        if (pct < 10) out->solid_col = BC_RED;
        return;
    }
    if (pct >= 100) {                      // charged: nothing left to fill
        out->solid = BATCELLS;
        out->solid_col = BC_GREEN;
        return;
    }
    int cell = pct / 25;                   // 0-24 % the first cell, 25-49 % the second...
    out->solid = cell;
    out->blink_col = pct < 10 ? BC_RED : pct >= 90 ? BC_GREEN : BC_GREY;
    if (blink_on) out->blink = cell;
}
