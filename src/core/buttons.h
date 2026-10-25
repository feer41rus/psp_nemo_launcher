// Button bits used by the portable core; values match PSP_CTRL_* from pspctrl.h.
#ifndef NEMO_BUTTONS_H
#define NEMO_BUTTONS_H

#define NB_SELECT   0x000001
#define NB_START    0x000008
#define NB_UP       0x000010
#define NB_RIGHT    0x000020
#define NB_DOWN     0x000040
#define NB_LEFT     0x000080
#define NB_LTRIGGER 0x000100
#define NB_RTRIGGER 0x000200
#define NB_TRIANGLE 0x001000
#define NB_CIRCLE   0x002000
#define NB_CROSS    0x004000
#define NB_SQUARE   0x008000
#define NB_HOME     0x010000
#define NB_VOLUP    0x100000
#define NB_VOLDOWN  0x200000
#define NB_NOTE     0x800000

#define NB_DIRS (NB_UP | NB_RIGHT | NB_DOWN | NB_LEFT)

#endif
