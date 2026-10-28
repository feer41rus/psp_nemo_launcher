#ifndef NEMO_CCUI_H
#define NEMO_CCUI_H

#include "ccdraw.h"
#include "ccmenu.h"

// Control Center window drawing (1.2.4), in the style of Nemo's power window.
#define CCUI_W 280
#define CCUI_H 176
#define CCUI_X ((480 - CCUI_W) / 2)
#define CCUI_Y ((272 - CCUI_H) / 2)

// Panel at (ox, oy): CCUI_X / CCUI_Y on the screen, or 0 / 0 in a panel-sized buffer.
// hour < 0: no clock; battery < 0: no charge.
void ccui_draw(CcFrame *fb, int ox, int oy, const CcMenu *m, int lang_en, int translucent, int hour, int minute,
               int battery);
void ccui_toast(CcFrame *fb, const char *text);   // small note at the top right

#endif
