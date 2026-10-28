#include "ccui.h"
#include <stdio.h>

#define COL_PANEL 0x141b26
#define COL_TEXT 0xe9eef5
#define COL_MUTED 0x8c9bb0
#define COL_SEL 0x2b5fa8
#define ROW_H 26

static void value(CcFrame *fb, int ox, int y, int v) {
    char s[16];
    snprintf(s, sizeof s, "‹ %d ›", v);
    ccdraw_text(fb, ox + CCUI_W - 16 - ccdraw_text_width(s), y, COL_TEXT, s);
}

void ccui_draw(CcFrame *fb, int ox, int oy, const CcMenu *m, int lang_en, int translucent, int hour, int minute, int battery) {
    ccdraw_round(fb, ox, oy, CCUI_W, CCUI_H, 10, COL_PANEL, translucent ? 210 : 255);
    char head[32];
    if (hour >= 0 && battery >= 0) snprintf(head, sizeof head, "%02d:%02d · %d %%", hour, minute, battery);
    else if (hour >= 0) snprintf(head, sizeof head, "%02d:%02d", hour, minute);
    else head[0] = 0;
    ccdraw_text(fb, ox + 16, oy + 8, COL_MUTED, head);
    int y0 = oy + 32;
    if (m->confirm) {
        ccdraw_text(fb, ox + 16, y0 + 10, COL_TEXT, ccmenu_text(CCT_ASK, lang_en));
        ccdraw_text(fb, ox + 16, y0 + 34, COL_MUTED, ccmenu_text(CCT_LOST, lang_en));
        for (int i = 0; i < 2; i++) {   // No (left) / Yes (right)
            int bx = ox + 40 + i * 110, by = oy + CCUI_H - 50, sel = (i == 1) == (m->confirm_yes != 0);
            if (sel) ccdraw_round(fb, bx, by, 90, 26, 8, COL_SEL, 255);
            const char *t = ccmenu_text(i ? CCT_YES : CCT_NO, lang_en);
            ccdraw_text(fb, bx + (90 - ccdraw_text_width(t)) / 2, by + 5, COL_TEXT, t);
        }
        return;
    }
    for (int i = 0; i < CCI_COUNT; i++) {
        int y = y0 + i * ROW_H;
        if (i == m->sel) ccdraw_round(fb, ox + 8, y, CCUI_W - 16, ROW_H - 2, 7, COL_SEL, 255);
        static const CcText T[CCI_COUNT] = { CCT_RESUME, CCT_SHOT, CCT_VOLUME, CCT_BRIGHT, CCT_EXIT };
        ccdraw_text(fb, ox + 18, y + 4, COL_TEXT, ccmenu_text(T[i], lang_en));
        if (i == CCI_VOLUME) value(fb, ox, y + 4, m->volume);
        if (i == CCI_BRIGHT) value(fb, ox, y + 4, m->bright + 1);
    }
}

void ccui_toast(CcFrame *fb, const char *text) {
    int w = ccdraw_text_width(text) + 28;
    ccdraw_round(fb, 480 - w - 10, 10, w, 28, 9, COL_PANEL, 255);
    ccdraw_round(fb, 480 - w - 10 + 8, 19, 4, 10, 2, 0x6fb0ff, 255);   // accent mark, as in Nemo
    ccdraw_text(fb, 480 - w - 10 + 18, 15, 0xffffff, text);
}
