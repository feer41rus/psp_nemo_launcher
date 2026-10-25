#include "mnav.h"
#include <string.h>

static int clampi(int v, int count) { return v < 0 ? 0 : (count <= 0 ? 0 : (v >= count ? count - 1 : v)); }

static MusicCmd cmd(MusicCmdType t, int index, int dir) {
    MusicCmd c = { t, index, dir };
    return c;
}

void mnav_init(MusicNav *n) {
    memset(n, 0, sizeof *n);
    n->stack[0].kind = MS_LIBRARY;
    n->depth = 1;
}

void mnav_push(MusicNav *n, MusicScreen s) {
    if (n->depth >= MNAV_DEPTH) return;
    if (s == MS_NOW) n->np_focus = n->np_last_button = NP_PLAY;
    n->stack[n->depth].kind = s;
    n->stack[n->depth].sel = s == MS_CONFIRM ? 1 : 0;   // dialogs default to "No"
    n->depth++;
}

void mnav_pop(MusicNav *n) {
    if (n->depth > 1) n->depth--;
}

MusicScreen mnav_top(const MusicNav *n) { return n->stack[n->depth - 1].kind; }
int mnav_sel(const MusicNav *n) { return n->stack[n->depth - 1].sel; }

static MusicCmd handle_library(MusicNav *n, Action a, const MusicModel *m) {
    n->side_sel = clampi(n->side_sel, m->side_count);
    n->track_sel = clampi(n->track_sel, m->track_count);
    switch (a) {
    case ACT_START:
        mnav_push(n, MS_NOW);
        return cmd(MC_OPEN_NOW, 0, 0);
    case ACT_MENU:
        mnav_push(n, MS_MENU);
        return cmd(MC_OPEN_MENU, n->column == 1 ? n->track_sel : n->side_sel, n->column);
    case ACT_BACK:
        n->column = 0;
        return cmd(MC_NONE, 0, 0);
    default:
        break;
    }
    if (n->column == 0) {
        int before = n->side_sel;
        if (a == ACT_UP) n->side_sel = clampi(n->side_sel - 1, m->side_count);
        if (a == ACT_DOWN) n->side_sel = clampi(n->side_sel + 1, m->side_count);
        if (n->side_sel != before) {
            n->track_sel = 0;
            return cmd(MC_SIDE_CHANGED, n->side_sel, 0);
        }
        if (a == ACT_CONFIRM || a == ACT_RIGHT) n->column = 1;
        return cmd(MC_NONE, 0, 0);
    }
    if (a == ACT_UP) n->track_sel = clampi(n->track_sel - 1, m->track_count);
    if (a == ACT_DOWN) n->track_sel = clampi(n->track_sel + 1, m->track_count);
    if (a == ACT_LEFT) n->column = 0;
    if (a == ACT_CONFIRM && m->track_count > 0) {
        mnav_push(n, MS_NOW);
        return cmd(MC_PLAY_TRACK, n->track_sel, 0);
    }
    return cmd(MC_NONE, 0, 0);
}

static MusicCmd handle_list(MusicNav *n, Action a, int count, MusicCmdType pick) {
    MusicNavScreen *s = &n->stack[n->depth - 1];
    if (a == ACT_UP) s->sel--;
    if (a == ACT_DOWN) s->sel++;
    s->sel = clampi(s->sel, count);
    if (a == ACT_CONFIRM && count > 0) return cmd(pick, s->sel, 0);
    return cmd(MC_NONE, 0, 0);
}

MusicCmd mnav_handle(MusicNav *n, Action a, const MusicModel *m) {
    MusicScreen top = mnav_top(n);
    if (a == ACT_BACK && top != MS_LIBRARY && top != MS_OSK) {
        mnav_pop(n);
        return cmd(MC_NONE, 0, 0);
    }
    switch (top) {
    case MS_LIBRARY:
        return handle_library(n, a, m);
    case MS_NOW:
        // Cursor over the controls (✕ presses the focused one); on the progress bar ←/→ seek.
        if (a == ACT_MENU) {
            mnav_push(n, MS_MENU);
            return cmd(MC_OPEN_MENU, 0, 2);   // dir 2 = opened from Now Playing
        }
        if (n->np_focus == NP_PROGRESS) {
            if (a == ACT_LEFT || a == ACT_RIGHT) return cmd(MC_SEEK, 0, a == ACT_LEFT ? -1 : 1);
            if (a == ACT_DOWN) n->np_focus = n->np_last_button;
            if (a == ACT_CONFIRM) return cmd(MC_TOGGLE_PAUSE, 0, 0);
            return cmd(MC_NONE, 0, 0);
        }
        if (a == ACT_LEFT && n->np_focus > NP_SHUFFLE) n->np_focus--;
        if (a == ACT_RIGHT && n->np_focus < NP_REPEAT) n->np_focus++;
        if (a == ACT_UP) {
            n->np_last_button = n->np_focus;
            n->np_focus = NP_PROGRESS;
        }
        if (a == ACT_CONFIRM) return cmd(MC_NP_BUTTON, n->np_focus, 0);
        return cmd(MC_NONE, 0, 0);
    case MS_MENU: {
        MusicNavScreen *s = &n->stack[n->depth - 1];
        if (a == ACT_LEFT || a == ACT_RIGHT) {
            s->sel = clampi(s->sel, m->menu_count);
            return m->menu_count > 0 ? cmd(MC_MENU_PICK, s->sel, a == ACT_LEFT ? -1 : 1) : cmd(MC_NONE, 0, 0);
        }
        return handle_list(n, a, m->menu_count, MC_MENU_PICK);
    }
    case MS_QUEUE:
        if (a == ACT_QUICK && m->queue_count > 0)
            return cmd(MC_QUEUE_REMOVE, clampi(mnav_sel(n), m->queue_count), 0);
        return handle_list(n, a, m->queue_count, MC_QUEUE_JUMP);
    case MS_PICK:
        return handle_list(n, a, m->pick_count, MC_PICK);
    case MS_CONFIRM: {
        MusicNavScreen *s = &n->stack[n->depth - 1];
        if (a == ACT_LEFT || a == ACT_RIGHT || a == ACT_UP || a == ACT_DOWN) s->sel = !s->sel;
        if (a == ACT_CONFIRM) {
            int yes = s->sel == 0;
            mnav_pop(n);
            return cmd(yes ? MC_CONFIRM_YES : MC_NONE, 0, 0);
        }
        return cmd(MC_NONE, 0, 0);
    }
    case MS_OSK:
    case MS_LISTEN:
        return cmd(MC_NONE, 0, 0);   // the UI routes these screens itself
    }
    return cmd(MC_NONE, 0, 0);
}
