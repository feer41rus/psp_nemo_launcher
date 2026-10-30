#ifndef NEMO_BATCELLS_H
#define NEMO_BATCELLS_H

// Battery icon at the top right (1.3.12): four cells of 25 %. On battery the started cells are filled, red
// under 10 %. While charging the cell the level is in blinks — red under 10 %, green from 90 %, grey between;
// a full battery on the charger is steady green.
typedef enum { BC_TEXT, BC_GREY, BC_GREEN, BC_RED } BatCellCol;

typedef struct {
    int solid;              // cells filled from the left
    int blink;              // the blinking cell (shown now), -1 none
    BatCellCol solid_col, blink_col;
} BatCells;

#define BATCELLS 4
#define BATCELL_BLINK_MS 500   // half a period: on, then off

// pct < 0: unknown (no cells). blink_on: the visible half of the blink.
void batcells(int pct, int charging, int blink_on, BatCells *out);

#endif
