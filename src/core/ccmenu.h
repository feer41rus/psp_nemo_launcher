#ifndef NEMO_CCMENU_H
#define NEMO_CCMENU_H

#include <stdint.h>

// The Control Center window (1.2.4): HOME in a game opens it; Resume, Screenshot, Volume, Brightness, Quit.
// Buttons in PSP_CTRL_* bits (kept here so the logic builds on a PC).
#define CC_UP 0x0010
#define CC_RIGHT 0x0020
#define CC_DOWN 0x0040
#define CC_LEFT 0x0080
#define CC_CIRCLE 0x2000
#define CC_CROSS 0x4000
#define CC_HOME 0x10000
#define CC_VOLUP 0x100000     // the VOL+ / VOL- keys (seen by kernel reads only)
#define CC_VOLDOWN 0x200000

typedef enum { CCI_RESUME, CCI_SHOT, CCI_VOLUME, CCI_BRIGHT, CCI_EXIT, CCI_COUNT } CcItem;
typedef enum { CCA_NONE, CCA_CLOSE, CCA_SHOT, CCA_EXIT, CCA_VOLUME, CCA_BRIGHT } CcAction;   // VOLUME/BRIGHT: changed

#define CC_VOLUME_MAX 30
#define CC_BRIGHT_MAX 3   // impose levels 0..3 = brightness 1..4

typedef struct {
    int open, sel;
    int confirm, confirm_yes;   // "Quit the game?" (default No)
    int volume, bright;
    int home_down;              // HOME held: a new press is needed to open again
    int cross_ok;               // the system's confirm button is cross (else circle)
} CcMenu;

void     ccmenu_init(CcMenu *m, int cross_ok);
int      ccmenu_home(CcMenu *m, int home_down, int allowed);   // 1 = open now
void     ccmenu_open(CcMenu *m, int volume, int bright);
CcAction ccmenu_input(CcMenu *m, uint32_t pressed);

typedef enum { CCT_RESUME, CCT_SHOT, CCT_VOLUME, CCT_BRIGHT, CCT_EXIT, CCT_ASK, CCT_LOST, CCT_YES, CCT_NO, CCT_SAVED,
               CCT_COUNT } CcText;
const char *ccmenu_text(CcText id, int lang_en);

#endif
