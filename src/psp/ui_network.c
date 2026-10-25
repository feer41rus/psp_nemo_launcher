#include "ui.h"
#include "gfx.h"
#include "usb.h"
#include <stdio.h>
#include <string.h>

// Layout F: section column on the left, panel of the selected section on the right.
#define COL_X 8
#define COL_W 112
#define SEC_H 24
#define PANEL_X 134
#define PANEL_W (SCREEN_W - PANEL_X - 10)
#define LIST_ROW_H 18
#define COL_OK 0x5fe0b0ff
#define COL_ERR 0xff6f7dff
#define COL_SEL 0x2b5fa8ff

static StrId section_label(NetSection s) {
    switch (s) {
    case NS_WIFI: return S_NET_WIFI;
    case NS_FTP: return S_NET_FTP;
    case NS_USB: return S_NET_USB;
    case NS_WPA2: return S_NET_WPA2;
    default: return S_NET_USBNET;
    }
}

// Text wrapped on spaces into at most max_lines lines; returns the height used.
static int text_wrap(FontId f, int x, int y, int w, uint32_t col, const char *text, int max_lines) {
    char line[160];
    const char *p = text;
    int lh = gfx_text_height(f), lines = 0;
    while (*p && lines < max_lines) {
        size_t best = 0, n = 0;
        while (p[n]) {
            size_t next = n;
            while (p[next] && p[next] != ' ') next++;
            if (next >= sizeof line) break;
            memcpy(line, p, next);
            line[next] = '\0';
            if (gfx_text_width(f, line) > w && best > 0) break;
            best = next;
            if (!p[next]) break;
            n = next + 1;
        }
        if (best == 0) best = strlen(p) < sizeof line - 1 ? strlen(p) : sizeof line - 1;
        memcpy(line, p, best);
        line[best] = '\0';
        if (lines == max_lines - 1 && p[best]) gfx_text_fit(f, x, y + lines * lh, w, col, p);
        else gfx_text(f, x, y + lines * lh, col, line);
        lines++;
        p += best;
        while (*p == ' ') p++;
    }
    return lines * lh;
}

static void signal_bars(int x, int y, int signal) {
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 2, on = signal >= (i == 0 ? 1 : i * 25);
        gfx_rect(x + i * 3, y + 9 - h, 2, h, on ? COL_OK : 0x3a4656ff);
    }
}

static void lock_icon(int x, int y, uint32_t col) {
    gfx_round_rect_outline(x + 1, y, 6, 7, 3, col);
    gfx_round_rect(x, y + 4, 8, 6, 1, col);
}

static void draw_column(const App *a) {
    const NetNav *n = &a->netnav;
    int y = CONTENT_Y + 8;
    for (int i = 0; i < n->section_count; i++, y += SEC_H) {
        int sel = i == n->section;
        if (sel) {
            if (n->focus == NF_COLUMN) gfx_round_rect(COL_X, y, COL_W, SEC_H - 2, 6, COL_SEL);
            else gfx_round_rect(COL_X, y, COL_W, SEC_H - 2, 6, 0x24324aff);
        }
        int disabled = n->sections[i] == NS_USBNET;
        gfx_text_fit(FONT_BODY, COL_X + 10, y + 3, COL_W - 14, disabled ? 0x5b6779ff : COL_TEXT,
                     tr(section_label(n->sections[i])));
    }
    gfx_rect(PANEL_X - 8, CONTENT_Y + 8, 1, CONTENT_H - 16, 0x222c3aff);
}

static void wifi_card(const App *a, int y) {
    const NetSnapshot *s = &a->net;
    gfx_round_rect(PANEL_X, y, PANEL_W, 38, 7, COL_PANEL);
    char line[160];
    if (!s->switch_on) {
        gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 3, PANEL_W - 20, COL_TEXT, tr(S_NET_NOT_CONNECTED));
        gfx_text_fit(FONT_SMALL, PANEL_X + 10, y + 21, PANEL_W - 20, COL_WARN, tr(S_NET_SWITCH_OFF));
    } else if (s->phase == NET_CONNECTED) {
        gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 3, PANEL_W - 20, COL_TEXT, s->ssid);
        snprintf(line, sizeof line, "%s · IP %s · %s %d%% · %s %d", tr(S_NET_CONNECTED), s->ip, tr(S_NET_SIGNAL),
                 s->signal, tr(S_NET_CHANNEL), s->channel);
        gfx_text_fit(FONT_SMALL, PANEL_X + 10, y + 21, PANEL_W - 20, COL_OK, line);
    } else {
        gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 3, PANEL_W - 20, COL_TEXT, tr(S_NET_NOT_CONNECTED));
        StrId st = s->phase == NET_STARTING || s->phase == NET_OFF ? S_NET_STARTING
                 : s->phase == NET_SCANNING ? S_NET_SCANNING : S_NET_NONE_FOUND;
        if (st != S_NET_NONE_FOUND || s->ap_count == 0)
            gfx_text_fit(FONT_SMALL, PANEL_X + 10, y + 21, PANEL_W - 20, COL_MUTED,
                         st == S_NET_NONE_FOUND && s->scan_ms == 0 ? tr(S_NET_SCANNING) : tr(st));
    }
}

static void wifi_connecting(const App *a, int y) {
    const NetSnapshot *s = &a->net;
    gfx_round_rect(PANEL_X, y, PANEL_W, 110, 7, COL_PANEL);
    char t[96];
    snprintf(t, sizeof t, tr(S_NET_CONNECTING_TO_FMT), s->target);
    gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 6, PANEL_W - 20, COL_TEXT, t);
    static const StrId steps[4] = { S_NET_STEP_SEARCH, S_NET_STEP_JOIN, S_NET_STEP_KEYS, S_NET_STEP_IP };
    for (int i = 0; i < 4; i++) {
        int yy = y + 30 + i * 19;
        uint32_t col = i < s->step ? COL_OK : i == s->step ? COL_WARN : COL_MUTED;
        const char *mark = i < s->step ? "+" : i == s->step ? ((a->now_ms / 300) % 2 ? "o" : "·") : "-";
        gfx_text(FONT_BODY, PANEL_X + 12, yy, col, mark);
        gfx_text_fit(FONT_BODY, PANEL_X + 28, yy, PANEL_W - 40, col, tr(steps[i]));
    }
}

static void wifi_error(const App *a, int y) {
    gfx_round_rect(PANEL_X, y, PANEL_W, 96, 7, COL_PANEL);
    gfx_rect(PANEL_X, y + 6, 3, 84, COL_ERR);
    gfx_text_fit(FONT_BOLD, PANEL_X + 12, y + 6, PANEL_W - 24, COL_TEXT, tr(S_NET_ERR_TITLE));
    if (a->net.target[0]) gfx_text_fit(FONT_SMALL, PANEL_X + 12, y + 26, PANEL_W - 24, COL_MUTED, a->net.target);
    text_wrap(FONT_BODY, PANEL_X + 12, y + 44, PANEL_W - 24, COL_TEXT, tr(a->net.error), 2);
}

static void wifi_list(App *a, int y) {
    const NetSnapshot *s = &a->net;
    int bottom = SCREEN_H - HINT_BAR_H - 4;
    char head[96];
    snprintf(head, sizeof head, "%s", tr(S_NET_NEARBY));
    if (s->scan_ms && (s->scan_refused || a->now_ms - s->scan_ms > 60000)) {
        char age[48];
        snprintf(age, sizeof age, tr(S_NET_UPDATED_MIN), (int)((a->now_ms - s->scan_ms) / 60000));
        snprintf(head + strlen(head), sizeof head - strlen(head), " · %s", age);
    }
    gfx_text(FONT_SMALL, PANEL_X + 2, y, COL_MUTED, head);
    if (s->phase == NET_SCANNING) gfx_text(FONT_SMALL, PANEL_X + PANEL_W - 4 - gfx_text_width(FONT_SMALL, tr(S_NET_SCANNING)),
                                           y, COL_ACCENT, tr(S_NET_SCANNING));
    y += 16;
    int visible = (bottom - y) / LIST_ROW_H;
    if (visible < 1) return;
    int sel = a->netnav.row;
    // Keep the selection in view; S_NET_SAVED header takes one row slot when it is shown.
    int first = sel - visible + 2;
    if (first < 0) first = 0;
    int saved_header_done = 0;
    for (int i = first; i < a->net_row_count && y + LIST_ROW_H <= bottom; i++) {
        const NetRow *r = &a->net_rows[i];
        if (r->kind == NROW_SAVED && !saved_header_done) {
            saved_header_done = 1;
            gfx_text(FONT_SMALL, PANEL_X + 2, y + 2, COL_MUTED, tr(S_NET_SAVED));
            y += LIST_ROW_H;
            if (y + LIST_ROW_H > bottom) break;
        }
        int focused = a->netnav.focus == NF_PANEL && i == sel;
        if (focused) gfx_round_rect(PANEL_X, y, PANEL_W, LIST_ROW_H - 1, 5, COL_SEL);
        const char *name;
        const char *right;
        int sig = -1, locked = 0;
        if (r->kind == NROW_AP) {
            const NetAp *ap = &s->aps[r->ap];
            name = ap->ssid;
            sig = ap->signal;
            locked = net_sec_kind(ap->sec) != NSEC_OPEN;
            right = r->connected ? tr(S_NET_CONNECTED_MARK) : locked ? "" : tr(S_NET_OPEN);
        } else {
            name = s->profs[r->profile].ssid;
            right = r->connected ? tr(S_NET_CONNECTED_MARK) : tr(S_NET_NOT_NEAR);
        }
        int x = PANEL_X + 6;
        if (sig >= 0) signal_bars(x, y + 4, sig);
        x += 16;
        int rw = gfx_text_width(FONT_SMALL, right) + (locked ? 14 : 0);
        gfx_text_fit(FONT_BODY, x, y + 1, PANEL_W - (x - PANEL_X) - rw - 10, COL_TEXT, name);
        int rx = PANEL_X + PANEL_W - 6;
        if (locked) { rx -= 9; lock_icon(rx, y + 4, focused ? 0xdce8ffff : COL_MUTED); rx -= 5; }
        int tw = gfx_text_width(FONT_SMALL, right);
        gfx_text(FONT_SMALL, rx - tw, y + 3, r->connected ? COL_OK : focused ? 0xdce8ffff : COL_MUTED, right);
        y += LIST_ROW_H;
    }
}

static void draw_wifi(App *a) {
    int y = CONTENT_Y + 8;
    if (a->net.phase == NET_CONNECTING) { wifi_connecting(a, y); return; }
    if (a->net.phase == NET_ERROR) { wifi_error(a, y); return; }
    wifi_card(a, y);
    if (a->net.switch_on) wifi_list(a, y + 46);
}

static void toggle_row(const App *a, int y, StrId label, int on) {
    int focused = a->netnav.focus == NF_PANEL;
    gfx_round_rect(PANEL_X, y, PANEL_W, 24, 6, focused ? COL_SEL : COL_PANEL);
    gfx_text_fit(FONT_BODY, PANEL_X + 10, y + 3, PANEL_W - 70, COL_TEXT, tr(label));
    int tx = PANEL_X + PANEL_W - 36, ty = y + 6;
    gfx_round_rect(tx, ty, 26, 13, 6, on ? COL_OK : 0x3a4656ff);
    gfx_round_rect(on ? tx + 14 : tx + 2, ty + 2, 10, 9, 4, 0xffffffff);
}

static void draw_wpa2(const App *a) {
    int y = CONTENT_Y + 8;
    toggle_row(a, y, S_NET_WPA2_ROW, a->wpa2_on);
    y += 32;
    y += text_wrap(FONT_SMALL, PANEL_X + 4, y, PANEL_W - 8, COL_MUTED, tr(S_NET_WPA2_DESC), 4) + 6;
    text_wrap(FONT_SMALL, PANEL_X + 4, y, PANEL_W - 8, COL_MUTED, tr(S_NET_WPA2_RESTART), 2);
}

static void fmt_bytes(uint64_t b, char *out, size_t cap) {
    if (b >= 1024ull * 1024 * 1024) snprintf(out, cap, "%.2f GB", (double)b / (1024.0 * 1024 * 1024));
    else if (b >= 1024 * 1024) snprintf(out, cap, "%.1f MB", (double)b / (1024.0 * 1024));
    else snprintf(out, cap, "%u KB", (unsigned)(b / 1024));
}

static void draw_ftp(const App *a) {
    int y = CONTENT_Y + 8;
    const FtpStatus *f = &a->ftp;
    toggle_row(a, y, S_NET_FTP, f->phase != FTP_OFF);
    y += 32;
    if (f->phase == FTP_WAIT_WIFI) {
        gfx_round_rect(PANEL_X, y, PANEL_W, 50, 7, COL_PANEL);
        gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 6, PANEL_W - 20, COL_WARN, tr(S_FTP_WAIT_WIFI));
        text_wrap(FONT_SMALL, PANEL_X + 10, y + 26, PANEL_W - 20, COL_MUTED, tr(S_FTP_NEED_WIFI), 1);
        return;
    }
    text_wrap(FONT_SMALL, PANEL_X + 4, y, PANEL_W - 8, COL_MUTED, tr(S_FTP_DESC), 4);
}

// Full screen while the server runs (author: like USB, so nothing else stays loaded).
static void draw_ftp_screen(const App *a) {
    const FtpStatus *f = &a->ftp;
    gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H - HINT_BAR_H, 0x16263aff, COL_BG);
    gfx_text(FONT_SMALL, 10, 4, COL_MUTED, tr(S_NET_FTP));
    int cx = SCREEN_W / 2, y = 26;
    const char *t = tr(S_FTP_ON_TITLE);
    gfx_text(FONT_TITLE, cx - gfx_text_width(FONT_TITLE, t) / 2, y, COL_TEXT, t);
    y += 30;
    const char *sub = tr(S_FTP_CONNECT_FROM_PC);
    gfx_text(FONT_SMALL, cx - gfx_text_width(FONT_SMALL, sub) / 2, y, COL_MUTED, sub);
    y += 22;
    char addr[48];
    snprintf(addr, sizeof addr, "ftp://%s", a->net.ip);
    const StrId labels[3] = { S_FTP_ADDRESS, S_FTP_LOGIN, S_FTP_PASSWORD };
    const char *values[3] = { addr, FTP_USER, f->password };
    gfx_round_rect(60, y, SCREEN_W - 120, 92, 9, COL_PANEL);
    for (int i = 0; i < 3; i++) {
        int ry = y + 8 + i * 28;
        gfx_text(FONT_SMALL, 76, ry + 5, COL_MUTED, tr(labels[i]));
        gfx_text_fit(FONT_TITLE, 150, ry, SCREEN_W - 220, COL_TEXT, values[i]);
    }
    y += 102;
    char line[96], amount[24];
    fmt_bytes(f->bytes, amount, sizeof amount);
    if (f->client_ip[0]) snprintf(line, sizeof line, tr(S_FTP_CLIENT_FMT), f->client_ip, amount);
    else snprintf(line, sizeof line, "%s", tr(S_FTP_WAITING));
    gfx_text(FONT_BODY, cx - gfx_text_width(FONT_BODY, line) / 2, y, f->client_ip[0] ? COL_OK : COL_MUTED, line);
    y += 22;
    const char *one = tr(S_FTP_ONE_CONNECTION);
    gfx_text_fit(FONT_SMALL, 20, y, SCREEN_W - 40, 0x5b6779ff, one);
}

static void draw_usb(const App *a) {
    int y = CONTENT_Y + 8;
    gfx_text_fit(FONT_BOLD, PANEL_X + 4, y, PANEL_W - 8, COL_TEXT, tr(S_USB_TITLE));
    y += 24;
    y += text_wrap(FONT_SMALL, PANEL_X + 4, y, PANEL_W - 8, COL_MUTED, tr(S_USB_DESC), 4) + 10;
    int w = 150;
    gfx_round_rect(PANEL_X + 4, y, w, 26, 13, a->netnav.focus == NF_PANEL ? COL_SEL : COL_PANEL);
    const char *t = tr(S_USB_CONNECT);
    gfx_text(FONT_BOLD, PANEL_X + 4 + (w - gfx_text_width(FONT_BOLD, t)) / 2, y + 4, COL_TEXT, t);
}

// Full screen while the PC holds the memory stick: what is going on and how to leave.
static void draw_usb_screen(const App *a) {
    gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H - HINT_BAR_H, 0x16263aff, COL_BG);
    gfx_text(FONT_SMALL, 10, 4, COL_MUTED, tr(S_USB_TITLE));
    int cx = SCREEN_W / 2, y = 40;
    const char *t = tr(S_USB_ACTIVE);
    gfx_text(FONT_TITLE, cx - gfx_text_width(FONT_TITLE, t) / 2, y, COL_TEXT, t);
    y += 38;
    int cable = usb_cable();
    gfx_round_rect(60, y, SCREEN_W - 120, 58, 9, COL_PANEL);
    text_wrap(FONT_BODY, 76, y + 8, SCREEN_W - 152, COL_TEXT, tr(cable ? S_USB_ACTIVE_DESC : S_USB_PLUG_CABLE), 2);
    y += 72;
    StrId st = usb_connected() ? S_USB_STATE_PC : cable ? S_USB_STATE_CABLE : S_USB_STATE_NO_CABLE;
    const char *s = tr(st);
    gfx_text(FONT_BODY, cx - gfx_text_width(FONT_BODY, s) / 2, y, st == S_USB_STATE_PC ? COL_OK : COL_WARN, s);
    y += 26;
    if (!cable) return;                       // the screen stays on until a cable is plugged in
    unsigned secs = (idle_remaining_ms(&a->idle, a->now_ms) + 999) / 1000;
    char line[96];
    snprintf(line, sizeof line, tr(S_USB_IDLE_FMT), (int)(secs / 60), (int)(secs % 60));
    gfx_text(FONT_SMALL, cx - gfx_text_width(FONT_SMALL, line) / 2, y, 0x5b6779ff, line);
}

static void draw_soon(StrId title, StrId desc) {
    int y = CONTENT_Y + 8;
    gfx_round_rect(PANEL_X, y, PANEL_W, 56, 7, COL_PANEL);
    gfx_text_fit(FONT_BOLD, PANEL_X + 10, y + 6, PANEL_W - 20, 0x5b6779ff, tr(title));
    text_wrap(FONT_SMALL, PANEL_X + 10, y + 28, PANEL_W - 20, COL_MUTED, tr(desc), 2);
}

static void draw_menu(const App *a) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
    int w = 220, h = 34 + 2 * 24 + 8, x = SCREEN_W - w - 12, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 10, 0x1a2230f8);
    gfx_text_fit(FONT_BOLD, x + 14, y + 9, w - 28, COL_TEXT, a->net.ssid);
    static const StrId items[2] = { S_NET_MENU_DISCONNECT, S_NET_MENU_FORGET };
    for (int i = 0; i < 2; i++) {
        int ry = y + 34 + i * 24;
        if (i == a->net_menu_sel) gfx_round_rect(x + 6, ry, w - 12, 22, 6, COL_SEL);
        gfx_text(FONT_BODY, x + 16, ry + 3, i == 1 ? 0xff8a8aff : COL_TEXT, tr(items[i]));
    }
}

static void draw_confirm(const App *a, const char *msg) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    int w = 320, h = 100, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    text_wrap(FONT_BODY, x + 16, y + 12, w - 32, COL_TEXT, msg, 2);
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, i == a->net_confirm_sel ? (i == 0 ? 0xd9534fff : COL_SEL) : COL_PANEL);
        const char *t = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}

void ui_network_draw(App *a) {
    if (a->net_screen == NETS_FTP) { draw_ftp_screen(a); return; }
    if (a->net_screen == NETS_USB) { draw_usb_screen(a); return; }
    draw_column(a);
    switch (netnav_current(&a->netnav)) {
    case NS_WIFI: draw_wifi(a); break;
    case NS_WPA2: draw_wpa2(a); break;
    case NS_FTP: draw_ftp(a); break;
    case NS_USB: draw_usb(a); break;
    case NS_USBNET: draw_soon(S_NET_USBNET, S_NET_SOON_13); break;
    }
    switch (a->net_screen) {
    case NETS_OSK: ui_osk_draw(a); break;
    case NETS_MENU: draw_menu(a); break;
    case NETS_CONFIRM_FORGET: {
        char msg[128];
        const char *name = "";
        for (int i = 0; i < a->net.prof_count; i++) if (a->net.profs[i].slot == a->net_slot) name = a->net.profs[i].ssid;
        snprintf(msg, sizeof msg, tr(S_NET_CONFIRM_FORGET), name);
        draw_confirm(a, msg);
        break;
    }
    case NETS_CONFIRM_RESCAN: draw_confirm(a, tr(S_NET_CONFIRM_RESCAN)); break;
    default: break;
    }
}
