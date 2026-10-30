#include "netnav.h"
#include <string.h>

void netnav_init(NetNav *n, int has_wifi, int is_go) {
    memset(n, 0, sizeof *n);
    n->has_wifi = has_wifi;
    if (has_wifi) {
        n->sections[n->section_count++] = NS_WIFI;
        n->sections[n->section_count++] = NS_FTP;
        n->sections[n->section_count++] = NS_USB;   // WPA2 lives in Settings › System (author, 1.1.4)
    } else {
        n->sections[n->section_count++] = NS_USB;
        n->sections[n->section_count++] = NS_USBNET;
    }
    if (is_go) n->sections[n->section_count++] = NS_BT;   // only the PSP Go has Bluetooth
}

NetSection netnav_current(const NetNav *n) { return n->sections[n->section]; }

static NetCmd cmd(NetCmdType t, int index) {
    NetCmd c = { t, index };
    return c;
}

// The one action of a single-row section (its toggle / button).
static NetCmd section_action(NetSection s) {
    switch (s) {
    case NS_FTP: return cmd(NC_FTP_TOGGLE, 0);
    case NS_USB: return cmd(NC_USB_OPEN, 0);
    case NS_WPA2: return cmd(NC_WPA2_TOGGLE, 0);
    default: return cmd(NC_NONE, 0);
    }
}

static NetCmd column(NetNav *n, Action a) {
    NetSection s = netnav_current(n);
    switch (a) {
    case ACT_UP: if (n->section > 0) n->section--; n->row = 0; break;
    case ACT_DOWN: if (n->section < n->section_count - 1) n->section++; n->row = 0; break;
    case ACT_BACK: return cmd(NC_BACK_TO_TAB, 0);
    case ACT_RIGHT:
    case ACT_CONFIRM:
        if (s == NS_USBNET) break;   // planned for 1.3
        n->focus = NF_PANEL;
        if (a == ACT_CONFIRM) return section_action(s);
        break;
    default: break;
    }
    return cmd(NC_NONE, 0);
}

static NetCmd panel(NetNav *n, Action a, const NetNavModel *m) {
    NetSection s = netnav_current(n);
    if (a == ACT_LEFT || a == ACT_BACK) {
        n->focus = NF_COLUMN;
        return cmd(NC_NONE, 0);
    }
    if (s != NS_WIFI) return a == ACT_CONFIRM ? section_action(s) : cmd(NC_NONE, 0);
    int rows = m ? m->rows : 0;
    switch (a) {
    case ACT_UP: if (n->row > 0) n->row--; break;
    case ACT_DOWN: if (n->row < rows - 1) n->row++; break;
    case ACT_QUICK: return cmd(NC_REFRESH, 0);
    case ACT_CONFIRM: if (rows > 0) return cmd(NC_ROW, n->row); break;
    case ACT_MENU: if (rows > 0) return cmd(NC_FORGET, n->row); break;
    default: break;
    }
    if (n->row >= rows) n->row = rows > 0 ? rows - 1 : 0;
    return cmd(NC_NONE, 0);
}

NetCmd netnav_handle(NetNav *n, Action a, const NetNavModel *m) {
    if (m && n->row >= m->rows) n->row = m->rows > 0 ? m->rows - 1 : 0;
    return n->focus == NF_COLUMN ? column(n, a) : panel(n, a, m);
}
