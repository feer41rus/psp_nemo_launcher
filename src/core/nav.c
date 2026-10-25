#include "nav.h"
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void nav_init(Nav *n) {
    memset(n, 0, sizeof *n);
    n->tab = TAB_HOME;
    n->shelf[TAB_HOME] = -1;
    n->card_disc = 1;
}

void nav_push(Nav *n, ScreenKind kind) {
    if (n->depth >= NAV_MAX_DEPTH) return;
    n->stack[n->depth].kind = kind;
    n->stack[n->depth].sel = kind == SCR_CONFIRM ? 1 : 0;   // dialogs default to "No"
    n->depth++;
}

void nav_pop(Nav *n) {
    if (n->depth > 0) n->depth--;
}

ScreenKind nav_top(const Nav *n) {
    return n->depth ? n->stack[n->depth - 1].kind : SCR_ROOT;
}

static int is_shelf_tab(Tab t) { return t == TAB_HOME || t == TAB_LIBRARY; }

// Keep shelf/tile focus inside what the model currently shows.
static void normalize(Nav *n, const NavModel *m) {
    Tab t = n->tab;
    if (is_shelf_tab(t)) {
        int lo = (t == TAB_HOME && m->has_continue) ? -1 : 0;
        int hi = m->shelf_count > 0 ? m->shelf_count - 1 : lo;
        n->shelf[t] = clampi(n->shelf[t], lo, hi);
        int s = n->shelf[t];
        int len = (s >= 0 && s < m->shelf_count && s < NAV_MAX_SHELVES) ? m->shelf_len[s] : 1;
        n->tile[t] = clampi(n->tile[t], 0, len > 0 ? len - 1 : 0);
    } else if (t == TAB_SETTINGS) {
        n->shelf[t] = clampi(n->shelf[t], 0, m->settings_count > 0 ? m->settings_count - 1 : 0);
    }
}

static NavCmd cmd(NavCmdType type, int index) {
    NavCmd c = { type, index };
    return c;
}

static int has_focus_item(const Nav *n, const NavModel *m) {
    int s = n->shelf[n->tab];
    if (s == -1) return m->has_continue;
    return s >= 0 && s < m->shelf_count && m->shelf_len[s] > 0;
}

static NavCmd handle_root(Nav *n, Action a, const NavModel *m) {
    Tab t = n->tab;
    if (is_shelf_tab(t)) {
        switch (a) {
        case ACT_UP: n->shelf[t]--; break;
        case ACT_DOWN: n->shelf[t]++; break;
        case ACT_LEFT: if (n->shelf[t] >= 0) n->tile[t]--; break;
        case ACT_RIGHT: if (n->shelf[t] >= 0) n->tile[t]++; break;
        case ACT_CONFIRM:
            if (!has_focus_item(n, m)) break;
            nav_push(n, SCR_CARD);
            return cmd(CMD_OPEN_CARD, n->tile[t]);
        case ACT_MENU: {   // also on an empty folder shelf: rename/delete it
            int sh = n->shelf[t];
            if (!has_focus_item(n, m) && !(sh >= 0 && sh < m->shelf_count)) break;
            nav_push(n, SCR_GAME_MENU);
            return cmd(CMD_OPEN_MENU, n->tile[t]);
        }
        case ACT_QUICK:
            if (!has_focus_item(n, m)) break;
            nav_push(n, SCR_FOLDER_PICK);
            return cmd(CMD_OPEN_PICK, n->tile[t]);
        default: break;
        }
        normalize(n, m);
    } else if (t == TAB_SETTINGS) {
        if (a == ACT_UP) n->shelf[t]--;
        if (a == ACT_DOWN) n->shelf[t]++;
        normalize(n, m);
        if (a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT)
            return cmd(CMD_SETTING, n->shelf[t]);
    }
    return cmd(CMD_NONE, 0);
}

static NavCmd handle_list(Nav *n, Action a, int count, NavCmdType pick) {
    NavScreen *s = &n->stack[n->depth - 1];
    if (a == ACT_UP) s->sel--;
    if (a == ACT_DOWN) s->sel++;
    s->sel = clampi(s->sel, 0, count > 0 ? count - 1 : 0);
    if (a == ACT_CONFIRM && count > 0) return cmd(pick, s->sel);
    return cmd(CMD_NONE, 0);
}

NavCmd nav_handle(Nav *n, Action a, const NavModel *m) {
    if (m->hidden_tabs & (1u << n->tab)) {    // hidden while open (Settings › Nemo › Music off)
        n->tab = TAB_HOME;
        n->depth = 0;
    }
    if (a == ACT_HOME) {
        n->tab = TAB_HOME;
        n->depth = 0;
        normalize(n, m);
        return cmd(CMD_NONE, 0);
    }
    if (a == ACT_TAB_PREV || a == ACT_TAB_NEXT) {
        if (nav_top(n) == SCR_OSK) return cmd(CMD_NONE, 0);   // L/R belong to the keyboard
        do n->tab = (Tab)((n->tab + (a == ACT_TAB_NEXT ? 1 : TAB_COUNT - 1)) % TAB_COUNT);
        while (m->hidden_tabs & (1u << n->tab));
        n->depth = 0;
        return cmd(CMD_NONE, 0);
    }
    if (a == ACT_BACK && n->depth > 0 && nav_top(n) != SCR_OSK) {
        nav_pop(n);
        return cmd(CMD_NONE, 0);
    }

    switch (nav_top(n)) {
    case SCR_ROOT:
        normalize(n, m);
        return handle_root(n, a, m);
    case SCR_CARD:
        if (m->card_discs > 1) {
            if (a == ACT_LEFT) n->card_disc--;
            if (a == ACT_RIGHT) n->card_disc++;
            n->card_disc = clampi(n->card_disc, 1, m->card_discs);
        }
        return a == ACT_CONFIRM ? cmd(CMD_LAUNCH, n->card_disc) : cmd(CMD_NONE, 0);
    case SCR_GAME_MENU:
        return handle_list(n, a, m->menu_count, CMD_MENU_PICK);
    case SCR_FOLDER_PICK:
        return handle_list(n, a, m->pick_count, CMD_FOLDER_PICK);
    case SCR_CONFIRM: {
        NavScreen *s = &n->stack[n->depth - 1];
        if (a == ACT_LEFT || a == ACT_RIGHT || a == ACT_UP || a == ACT_DOWN) s->sel = !s->sel;
        if (a == ACT_CONFIRM) {
            int yes = s->sel == 0;
            nav_pop(n);
            return cmd(yes ? CMD_CONFIRM_YES : CMD_NONE, 0);
        }
        return cmd(CMD_NONE, 0);
    }
    case SCR_OSK:
        return cmd(CMD_NONE, 0);   // the UI routes input to osk_handle
    }
    return cmd(CMD_NONE, 0);
}
