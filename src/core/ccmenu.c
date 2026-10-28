#include "ccmenu.h"
#include <string.h>

void ccmenu_init(CcMenu *m, int cross_ok) {
    memset(m, 0, sizeof *m);
    m->cross_ok = cross_ok;
}

int ccmenu_home(CcMenu *m, int home_down, int allowed) {
    int edge = home_down && !m->home_down;
    m->home_down = home_down;
    return edge && allowed && !m->open;
}

void ccmenu_open(CcMenu *m, int volume, int bright) {
    m->open = 1;
    m->sel = CCI_RESUME;
    m->confirm = m->confirm_yes = 0;
    m->volume = volume;
    m->bright = bright;
}

static CcAction close_with(CcMenu *m, CcAction a) {
    m->open = 0;
    m->confirm = 0;
    return a;
}

CcAction ccmenu_input(CcMenu *m, uint32_t pressed) {
    if (!m->open) return CCA_NONE;
    uint32_t ok = m->cross_ok ? CC_CROSS : CC_CIRCLE, back = m->cross_ok ? CC_CIRCLE : CC_CROSS;
    if (pressed & CC_HOME) m->home_down = 1;
    if (m->confirm) {
        if (pressed & (CC_LEFT | CC_RIGHT)) m->confirm_yes = !m->confirm_yes;
        if (pressed & (back | CC_HOME)) m->confirm = 0;
        else if (pressed & ok) {
            if (m->confirm_yes) return close_with(m, CCA_EXIT);
            m->confirm = 0;
        }
        return CCA_NONE;
    }
    if (pressed & (back | CC_HOME)) return close_with(m, CCA_CLOSE);
    int vol = (pressed & CC_VOLUP) ? 1 : (pressed & CC_VOLDOWN) ? -1 : 0;   // the volume keys: from any row
    if (vol) {
        int v = m->volume + vol;
        if (v < 0 || v > CC_VOLUME_MAX) return CCA_NONE;
        m->volume = v;
        return CCA_VOLUME;
    }
    if ((pressed & CC_UP) && m->sel > 0) m->sel--;
    if ((pressed & CC_DOWN) && m->sel < CCI_COUNT - 1) m->sel++;
    int step = (pressed & CC_RIGHT) ? 1 : (pressed & CC_LEFT) ? -1 : 0;
    if (step && m->sel == CCI_VOLUME) {
        int v = m->volume + step;
        if (v < 0 || v > CC_VOLUME_MAX) return CCA_NONE;
        m->volume = v;
        return CCA_VOLUME;
    }
    if (step && m->sel == CCI_BRIGHT) {
        int v = m->bright + step;
        if (v < 0 || v > CC_BRIGHT_MAX) return CCA_NONE;
        m->bright = v;
        return CCA_BRIGHT;
    }
    if (pressed & ok) {
        if (m->sel == CCI_RESUME) return close_with(m, CCA_CLOSE);
        if (m->sel == CCI_SHOT) return close_with(m, CCA_SHOT);
        if (m->sel == CCI_EXIT) {
            m->confirm = 1;
            m->confirm_yes = 0;
        }
    }
    return CCA_NONE;
}

const char *ccmenu_text(CcText id, int lang_en) {
    static const char *const RU[CCT_COUNT] = { "К игре", "Скриншот", "Громкость", "Яркость", "Выйти из игры",
                                               "Выйти из игры?", "Несохранённый прогресс пропадёт", "Да", "Нет",
                                               "Скриншот сохранён" };
    static const char *const EN[CCT_COUNT] = { "Resume", "Screenshot", "Volume", "Brightness", "Quit game",
                                               "Quit the game?", "Unsaved progress will be lost", "Yes", "No",
                                               "Screenshot saved" };
    return id < CCT_COUNT ? (lang_en ? EN[id] : RU[id]) : "";
}
