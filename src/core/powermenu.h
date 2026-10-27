#ifndef NEMO_POWERMENU_H
#define NEMO_POWERMENU_H

#include <stdint.h>
#include "input.h"

// Power window opened by holding HOME.
typedef enum { PI_SLEEP, PI_REBOOT, PI_SHUTDOWN, PI_EXIT_XMB, PI_CLOSE } PowerItem;

typedef struct {
    int open, sel, count;
    PowerItem items[5];
    uint32_t opened_ms;
} PowerMenu;

// Backdrop behind the window: black at POWER_DIM_ALPHA, fading in over POWER_DIM_FADE_MS.
#define POWER_DIM_ALPHA 0x99
#define POWER_DIM_FADE_MS 150

void powermenu_open(PowerMenu *m, int show_exit_xmb, uint32_t now_ms);
int powermenu_dim_alpha(const PowerMenu *m, uint32_t now_ms);   // 0 when closed
// UP/DOWN move (clamped). CONFIRM closes and returns the chosen item; BACK / HOME close and return
// PI_CLOSE. Anything else returns -1 and keeps the window open.
int powermenu_handle(PowerMenu *m, Action a);

#endif
