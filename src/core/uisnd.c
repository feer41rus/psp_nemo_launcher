#include "uisnd.h"
#include <string.h>

#define MOVE_GAP_MS 83     // ~12 ticks a second
#define EDGE_GAP_MS 250
#define TOAST_QUIET_MS 150

void uisnd_init(UiSndState *st) { memset(st, 0, sizeof *st); }

static int open_gate(const UiSndGate *g) { return g->enabled && !g->music_playing && !g->screen_off && !g->quiet; }

static UiSound played(UiSndState *st, UiSound s, uint32_t now_ms) {
    st->last_any_ms = now_ms;
    st->any = 1;
    return s;
}

UiSound uisnd_pick(UiSndState *st, const UiSndGate *g, Action act, int moved, uint32_t now_ms) {
    if (!open_gate(g)) return UIS_NONE;
    switch (act) {
    case ACT_UP: case ACT_DOWN: case ACT_LEFT: case ACT_RIGHT:
        if (moved) {
            if (st->last_move_ms && now_ms - st->last_move_ms < MOVE_GAP_MS) return UIS_NONE;
            st->last_move_ms = now_ms ? now_ms : 1;
            return played(st, g->settings ? UIS_SET_MOVE : UIS_MOVE, now_ms);
        }
        if (st->last_edge_ms && now_ms - st->last_edge_ms < EDGE_GAP_MS) return UIS_NONE;
        st->last_edge_ms = now_ms ? now_ms : 1;
        return played(st, UIS_EDGE, now_ms);
    case ACT_CONFIRM: return played(st, UIS_CONFIRM, now_ms);
    case ACT_BACK: return played(st, UIS_BACK, now_ms);
    case ACT_TAB_PREV: case ACT_TAB_NEXT: return played(st, UIS_TAB, now_ms);
    case ACT_MENU: return played(st, UIS_MENU, now_ms);
    default: return UIS_NONE;
    }
}

UiSound uisnd_toast(UiSndState *st, const UiSndGate *g, uint32_t now_ms) {
    if (!open_gate(g)) return UIS_NONE;
    if (st->any && now_ms - st->last_any_ms < TOAST_QUIET_MS) return UIS_NONE;
    return played(st, UIS_MENU, now_ms);
}
