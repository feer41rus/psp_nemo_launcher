#ifndef NEMO_POWERMENU_H
#define NEMO_POWERMENU_H

#include "input.h"

// Power window opened by holding HOME.
typedef enum { PI_SLEEP, PI_REBOOT, PI_SHUTDOWN, PI_EXIT_XMB, PI_CLOSE } PowerItem;

typedef struct {
    int open, sel, count;
    PowerItem items[5];
} PowerMenu;

void powermenu_open(PowerMenu *m, int show_exit_xmb);
// UP/DOWN move (clamped). CONFIRM closes and returns the chosen item; BACK / HOME close and return
// PI_CLOSE. Anything else returns -1 and keeps the window open.
int powermenu_handle(PowerMenu *m, Action a);

#endif
