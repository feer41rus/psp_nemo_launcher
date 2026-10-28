#include "setmenu.h"
#include <stddef.h>

static const SetEntry ENTRIES[] = {
    { SID_ROOT, SK_SUBMENU, S_TAB_SETTINGS, NULL, 0, 0, 0 },
    { SID_TIME, SK_SUBMENU, S_ST_TIME, NULL, 0, 0, 0 },
    { SID_SYSTEM, SK_SUBMENU, S_ST_SYSTEM, NULL, 0, 0, 0 },
    { SID_PLUGINS, SK_ACTION, S_ST_PLUGINS, NULL, 0, 0, 0 },
    { SID_ABOUT_NEMO, SK_SUBMENU, S_ST_ABOUT_NEMO, NULL, 0, 0, 0 },
    { SID_ABOUT_PSP, SK_SUBMENU, S_ST_ABOUT_PSP, NULL, 0, 0, 0 },
    { SID_POWER, SK_SUBMENU, S_ST_POWER, NULL, 0, 0, 0 },
    { SID_EXIT, SK_SUBMENU, S_ST_EXIT, NULL, 0, 0, 0 },
    { SID_DATETIME, SK_ACTION, S_ST_DATETIME, NULL, 0, 0, 0 },
    { SID_DATE_FORMAT, SK_CHOICE, S_ST_DATE_FORMAT, NULL, 0, 0, 0 },
    { SID_TIME_FORMAT, SK_CHOICE, S_ST_TIME_FORMAT, NULL, 0, 0, 0 },
    { SID_TIMEZONE, SK_CHOICE, S_ST_TIMEZONE, NULL, 0, 1, 0 },
    { SID_NICKNAME, SK_ACTION, S_ST_NICKNAME, NULL, 0, 0, 0 },
    { SID_LANGUAGE, SK_CHOICE, S_ST_LANGUAGE, NULL, 0, 0, 0 },
    { SID_WPA2, SK_CHOICE, S_ST_WPA2, NULL, 1, 0, 0 },
    { SID_POWERSAVE, SK_SUBMENU, S_ST_POWERSAVE, NULL, 0, 0, 0 },
    { SID_ARK, SK_SUBMENU, S_ST_ARK, NULL, 0, 0, 0 },
    { SID_NEMO, SK_SUBMENU, S_ST_NEMO, NULL, 0, 0, 0 },
    { SID_OVERCLOCK, SK_SUBMENU, S_ST_OVERCLOCK, NULL, 0, 0, 0 },
    { SID_CONFIRM, SK_CHOICE, S_ST_CONFIRM, NULL, 0, 0, 0 },
    { SID_RESET, SK_ACTION, S_ST_RESET, NULL, 0, 0, 0 },
    { SID_BRIGHTNESS, SK_CHOICE, S_ST_BRIGHTNESS, NULL, 0, 0, 0 },
    { SID_BACKLIGHT, SK_CHOICE, S_ST_BACKLIGHT, NULL, 0, 0, 0 },
    { SID_AUTOSLEEP, SK_CHOICE, S_ST_AUTOSLEEP, NULL, 0, 0, 0 },
    { SID_USBCHARGE, SK_CHOICE, S_ST_USBCHARGE, NULL, 0, 0, 0 },
    { SID_HIGHMEM, SK_CHOICE, S_ST_HIGHMEM, NULL, 0, 0, 0 },
    { SID_MSCACHE, SK_CHOICE, S_ST_MSCACHE, NULL, 0, 0, 0 },
    { SID_INFERNO, SK_CHOICE, S_ST_INFERNO, NULL, 0, 0, 0 },
    { SID_SKIPLOGO, SK_CHOICE, S_ST_SKIPLOGO, NULL, 0, 0, 0 },
    { SID_NOLED, SK_CHOICE, S_ST_NOLED, NULL, 0, 0, 0 },
    { SID_NOUMD, SK_CHOICE, S_ST_NOUMD, NULL, 0, 0, 0 },
    { SID_NOANALOG, SK_CHOICE, S_ST_NOANALOG, NULL, 0, 0, 0 },
    { SID_UMDREGION, SK_CHOICE, S_ST_UMDREGION, NULL, 0, 0, 0 },
    { SID_SHOW_XMB_EXIT, SK_CHOICE, S_ST_SHOW_XMB_EXIT, NULL, 0, 0, 0 },
    { SID_NCL_LOGO, SK_CHOICE, S_ST_NCL_LOGO, NULL, 0, 0, 0 },
    { SID_SPLASH_HELLO, SK_CHOICE, S_ST_SPLASH_HELLO, NULL, 0, 0, 0 },
    { SID_SPLASH_NCL, SK_CHOICE, S_ST_SPLASH_NCL, NULL, 0, 0, 0 },
    { SID_UI_SOUNDS, SK_CHOICE, S_ST_UI_SOUNDS, NULL, 0, 0, 0 },
    { SID_CONTROL_CENTER, SK_CHOICE, S_ST_CONTROL_CENTER, NULL, 0, 0, 0 },
    { SID_SITE, SK_CHOICE, S_ST_SITE, NULL, 0, 0, 0 },
    { SID_SHOW_MUSIC, SK_CHOICE, S_ST_SHOW_MUSIC, NULL, 0, 0, 0 },
    { SID_SHOW_FILES, SK_SOON, S_ST_SHOW_FILES, "1.1.7", 0, 0, 0 },
    { SID_SHOW_VIDEO, SK_SOON, S_ST_SHOW_VIDEO, "1.1.7", 0, 0, 0 },
    { SID_REPLACE_XMB, SK_CHOICE, S_ST_REPLACE_XMB, NULL, 0, 0, 0 },
    { SID_OC_GAMES, SK_CHOICE, S_ST_OC_GAMES, NULL, 0, 1, 0 },
    { SID_OC_XMB, SK_CHOICE, S_ST_OC_XMB, NULL, 0, 1, 0 },
    { SID_OC_NEMO, SK_CHOICE, S_ST_OC_NEMO, NULL, 0, 1, 0 },
    { SID_NEMO_VERSION, SK_ACTION, S_ST_NEMO_VERSION, NULL, 0, 0, 0 },   // opens "Changes" (1.2.0)
    { SID_CREATORS, SK_ACTION, S_ST_CREATORS, NULL, 0, 0, 0 },
    { SID_TESTERS, SK_ACTION, S_ST_TESTERS, NULL, 0, 0, 0 },
    { SID_THANKS, SK_ACTION, S_ST_THANKS, NULL, 0, 0, 0 },
    { SID_GITHUB, SK_INFO, S_ST_GITHUB, NULL, 0, 0, 0 },
    { SID_SITE_INFO, SK_INFO, S_ST_SITE_INFO, NULL, 0, 0, 1 },
    { SID_SAVES, SK_ACTION, S_ST_SAVES, NULL, 0, 0, 0 },
    { SID_MODEL, SK_INFO, S_ST_MODEL, NULL, 0, 0, 0 },
    { SID_BATTERY, SK_SUBMENU, S_ST_BATTERY, NULL, 0, 0, 0 },   // 1.2.2
    { SID_SLEEP, SK_ACTION, S_ST_SLEEP, NULL, 0, 0, 0 },
    { SID_RESTART, SK_ACTION, S_ST_RESTART, NULL, 0, 0, 0 },
    { SID_SHUTDOWN, SK_ACTION, S_ST_SHUTDOWN, NULL, 0, 0, 0 },
    { SID_TO_ARK, SK_ACTION, S_ST_TO_ARK, NULL, 0, 0, 0 },
    { SID_TO_XMB, SK_ACTION, S_ST_TO_XMB, NULL, 0, 0, 0 },
    { SID_DEBUG, SK_SUBMENU, S_ST_DEBUG, NULL, 0, 0, 0 },
    { SID_DBG_RAM, SK_CHOICE, S_ST_DBG_RAM, NULL, 0, 0, 0 },
    { SID_DBG_CPU, SK_CHOICE, S_ST_DBG_CPU, NULL, 0, 0, 0 },
    { SID_DBG_FPS, SK_CHOICE, S_ST_DBG_FPS, NULL, 0, 0, 0 },
    { SID_LOG_KEEP, SK_CHOICE, S_ST_LOG_KEEP, NULL, 0, 1, 0 },
    { SID_BAT_CHARGE, SK_INFO, S_BAT_CHARGE, NULL, 0, 0, 0 },
    { SID_BAT_STATE, SK_INFO, S_BAT_STATE, NULL, 0, 0, 0 },
    { SID_BAT_DETAILS, SK_ACTION, S_BAT_DETAILS, NULL, 0, 0, 0 },
    { SID_BAT_TEST, SK_ACTION, S_BAT_TEST, NULL, 0, 0, 0 },
};

// Children of every submenu, in the author's order; SID_ROOT ends a list (it is nobody's child).
static const SetId CHILDREN[SID_COUNT][SETMENU_MAX_ITEMS] = {
    [SID_ROOT] = { SID_TIME, SID_SYSTEM, SID_PLUGINS, SID_SAVES, SID_ABOUT_NEMO, SID_ABOUT_PSP, SID_POWER, SID_EXIT, SID_ROOT },
    [SID_TIME] = { SID_DATETIME, SID_DATE_FORMAT, SID_TIME_FORMAT, SID_TIMEZONE, SID_ROOT },
    [SID_SYSTEM] = { SID_NICKNAME, SID_LANGUAGE, SID_WPA2, SID_POWERSAVE, SID_ARK, SID_NEMO, SID_CONFIRM, SID_DEBUG, SID_RESET, SID_ROOT },
    [SID_POWERSAVE] = { SID_BRIGHTNESS, SID_BACKLIGHT, SID_AUTOSLEEP, SID_ROOT },
    [SID_ARK] = { SID_USBCHARGE, SID_HIGHMEM, SID_MSCACHE, SID_INFERNO, SID_SKIPLOGO, SID_NOLED, SID_NOUMD, SID_NOANALOG, SID_UMDREGION, SID_OVERCLOCK, SID_ROOT },
    [SID_NEMO] = { SID_SHOW_XMB_EXIT, SID_NCL_LOGO, SID_SPLASH_HELLO, SID_SPLASH_NCL, SID_UI_SOUNDS, SID_CONTROL_CENTER, SID_SITE, SID_SHOW_MUSIC, SID_REPLACE_XMB, SID_ROOT },
    [SID_DEBUG] = { SID_DBG_RAM, SID_DBG_CPU, SID_DBG_FPS, SID_LOG_KEEP, SID_ROOT },
    [SID_OVERCLOCK] = { SID_OC_GAMES, SID_OC_XMB, SID_OC_NEMO, SID_ROOT },
    [SID_ABOUT_NEMO] = { SID_NEMO_VERSION, SID_CREATORS, SID_TESTERS, SID_THANKS, SID_GITHUB, SID_SITE_INFO, SID_ROOT },
    [SID_ABOUT_PSP] = { SID_MODEL, SID_BATTERY, SID_ROOT },
    [SID_BATTERY] = { SID_BAT_CHARGE, SID_BAT_STATE, SID_BAT_DETAILS, SID_BAT_TEST, SID_ROOT },
    [SID_POWER] = { SID_SLEEP, SID_RESTART, SID_SHUTDOWN, SID_ROOT },
    [SID_EXIT] = { SID_TO_ARK, SID_TO_XMB, SID_ROOT },
};

const SetEntry *setmenu_entry(SetId id) {
    for (size_t i = 0; i < sizeof ENTRIES / sizeof ENTRIES[0]; i++)
        if (ENTRIES[i].id == id) return &ENTRIES[i];
    return &ENTRIES[0];
}

void setmenu_init(SetMenu *m, int has_wifi) {
    m->depth = 0;
    m->has_wifi = has_wifi;
    m->stack[0].node = SID_ROOT;
    m->stack[0].sel = m->stack[0].top = 0;
}

int setmenu_items(const SetMenu *m, SetId node, const SetEntry **out, int max) {
    int n = 0;
    for (int i = 0; i < SETMENU_MAX_ITEMS && CHILDREN[node][i] != SID_ROOT && n < max; i++) {
        const SetEntry *e = setmenu_entry(CHILDREN[node][i]);
        if (e->wifi_only && !m->has_wifi) continue;
        if (e->site_only && !m->show_site) continue;
        out[n++] = e;
    }
    return n;
}

SetId setmenu_node(const SetMenu *m) { return m->stack[m->depth].node; }

SetId setmenu_current(const SetMenu *m) {
    const SetEntry *e[SETMENU_MAX_ITEMS];
    int n = setmenu_items(m, setmenu_node(m), e, SETMENU_MAX_ITEMS);
    int sel = m->stack[m->depth].sel;
    return n > 0 && sel >= 0 && sel < n ? e[sel]->id : SID_ROOT;
}

static SetCmd cmd(SetCmdType t, SetId id, int delta) {
    SetCmd c = { t, id, delta };
    return c;
}

static void keep_visible(SetLevel *l, int count, int visible) {
    if (l->sel < l->top) l->top = l->sel;
    if (visible > 0 && l->sel >= l->top + visible) l->top = l->sel - visible + 1;
    if (l->top > count - visible) l->top = count - visible;
    if (l->top < 0) l->top = 0;
}

static int go_back(SetMenu *m) {
    if (m->depth == 0) return 0;
    m->depth--;
    return 1;
}

SetCmd setmenu_handle(SetMenu *m, Action a, int visible_rows) {
    SetLevel *l = &m->stack[m->depth];
    const SetEntry *e[SETMENU_MAX_ITEMS];
    int n = setmenu_items(m, l->node, e, SETMENU_MAX_ITEMS);
    if (l->sel >= n) l->sel = n - 1;
    if (l->sel < 0) l->sel = 0;
    const SetEntry *cur = n ? e[l->sel] : NULL;
    switch (a) {
    case ACT_UP: if (l->sel > 0) l->sel--; break;
    case ACT_DOWN: if (l->sel < n - 1) l->sel++; break;
    case ACT_BACK: go_back(m); return cmd(SC_NONE, SID_ROOT, 0);
    case ACT_LEFT:
        if (cur && cur->kind == SK_CHOICE) return cmd(SC_CHANGE, cur->id, -1);
        go_back(m);
        return cmd(SC_NONE, SID_ROOT, 0);
    case ACT_RIGHT: case ACT_CONFIRM:
        if (!cur) break;
        switch (cur->kind) {
        case SK_SUBMENU:
            if (m->depth + 1 >= SETMENU_DEPTH) break;
            m->depth++;
            m->stack[m->depth].node = cur->id;
            m->stack[m->depth].sel = m->stack[m->depth].top = 0;
            return cmd(SC_OPEN, cur->id, 0);
        case SK_CHOICE: return cmd(a == ACT_CONFIRM && cur->dropdown ? SC_PICK : SC_CHANGE, cur->id, 1);
        case SK_ACTION: if (a == ACT_CONFIRM) return cmd(SC_ACTIVATE, cur->id, 0); break;
        case SK_SOON: if (a == ACT_CONFIRM) return cmd(SC_SOON, cur->id, 0); break;
        default: break;
        }
        break;
    default: break;
    }
    keep_visible(l, n, visible_rows);
    return cmd(SC_NONE, SID_ROOT, 0);
}

void setmenu_scrollbar(int count, int top, int visible, int track_h, int *thumb_y, int *thumb_h) {
    *thumb_y = 0;
    *thumb_h = 0;
    if (count <= visible || visible <= 0) return;
    *thumb_h = track_h * visible / count;
    if (*thumb_h < 8) *thumb_h = 8;
    *thumb_y = (track_h - *thumb_h) * top / (count - visible);
}
