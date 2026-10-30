#include "ui.h"
#include "photo_hw.h"
#include "device.h"
#include "log.h"
#include "platform.h"
#include "art.h"
#include "launch.h"
#include "usb.h"
#include "bt_hw.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define AUTOSCAN_AGE_MS 30000

void network_init(App *a) {
    a->has_wifi = device_has_wifi(platform_model());
    netnav_init(&a->netnav, a->has_wifi, platform_model() == 4);
    a->net_screen = NETS_MAIN;
    a->wpa2_on = a->has_wifi ? platform_ark_get("wpa2", 1) : 0;
}

static int wifi_section(const App *a) { return netnav_current(&a->netnav) == NS_WIFI; }

// PSP Go Bluetooth (1.3.12): the registry every 10 s; a change goes to the detailed log.
static void bt_tick(App *a) {
    if (platform_model() != 4 || usb_active()) return;
    if (a->bt_at && a->now_ms - a->bt_at < 10000) return;
    BtState was = a->bt;
    bt_refresh(&a->bt, 0);
    if (!a->bt_at || memcmp(&was, &a->bt, sizeof was) != 0) {
        if (log_verbose) bt_log_state(a->bt_at ? "changed" : "first read", &a->bt);
    }
    a->bt_at = a->now_ms ? a->now_ms : 1;
}

void network_tick(App *a) {
    bt_tick(a);
    if (!a->has_wifi) return;
    if (a->nav.tab == TAB_NETWORK && !net_started()) net_start();   // modules load on first visit
    net_snapshot(&a->net);
    if (ftpsrv_tick(a->net.phase == NET_CONNECTED)) {   // Wi-Fi lost: FTP switched itself off
        if (a->net_screen == NETS_FTP) a->net_screen = NETS_MAIN;
        ui_toast(a, tr(S_FTP_AUTO_OFF));
        media_resume(a);
    }
    ftpsrv_status(&a->ftp);
    // Switched on while waiting for Wi-Fi: open the FTP screen as soon as the server runs.
    if (a->ftp.phase == FTP_ON && a->net_screen == NETS_MAIN) {
        a->net_screen = NETS_FTP;
        a->nav.tab = TAB_NETWORK;
    }
    a->net_row_count = net_rows(a->net.aps, a->net.ap_count, a->net.profs, a->net.prof_count,
                                a->net.phase == NET_CONNECTED ? a->net.ssid : "", a->net_rows, NET_ROWS_MAX);
    // Search by itself when the Wi-Fi section is on screen and the list is missing or old.
    if (a->nav.tab == TAB_NETWORK && wifi_section(a) && a->net.switch_on && a->net.phase == NET_IDLE &&
        (a->net.scan_ms == 0 || a->now_ms - a->net.scan_ms > AUTOSCAN_AGE_MS)) {
        static uint32_t asked_ms;
        if (!asked_ms || a->now_ms - asked_ms > AUTOSCAN_AGE_MS) {
            asked_ms = a->now_ms;
            net_scan();
        }
    }
}

int network_fullscreen(const App *a) {
    return a->nav.tab == TAB_NETWORK && (a->net_screen == NETS_USB || a->net_screen == NETS_FTP);
}

int network_owns_all(const App *a) { return a->net_screen == NETS_OSK || a->net_screen == NETS_USB || a->net_screen == NETS_FTP; }

static void open_password(App *a, const NetAp *ap) {
    a->net_target = *ap;
    a->osk_purpose = OSK_FOR_WIFI_PASSWORD;
    osk_init_mode(&a->osk, "", OSK_MODE_PASSWORD);
    NetSecKind k = net_sec_kind(ap->sec);
    if (k == NSEC_WPA) osk_set_length_rule(&a->osk, 8, 63);
    else osk_set_length_rule(&a->osk, 5, 26);
    a->net_screen = NETS_OSK;
}

static void connect_row(App *a, int index) {
    if (index < 0 || index >= a->net_row_count) return;
    const NetRow *r = &a->net_rows[index];
    if (r->connected) {                       // Disconnect / Forget
        a->net_menu_sel = 0;
        a->net_slot = r->profile >= 0 ? a->net.profs[r->profile].slot : -1;
        a->net_screen = NETS_MENU;
        return;
    }
    if (r->kind == NROW_SAVED) {
        a->net_target_pw = 0;
        net_connect_profile(a->net.profs[r->profile].slot);
        return;
    }
    const NetAp *ap = &a->net.aps[r->ap];
    a->net_target = *ap;
    if (r->profile >= 0 || net_sec_kind(ap->sec) == NSEC_OPEN) {
        a->net_target_pw = 0;
        net_connect_ap(ap, NULL);
    } else {
        open_password(a, ap);
    }
}

static void ask_forget(App *a, int index) {
    if (index < 0 || index >= a->net_row_count || a->net_rows[index].profile < 0) return;
    a->net_slot = a->net.profs[a->net_rows[index].profile].slot;
    a->net_confirm_sel = 1;                   // "No" first: forgetting is not undoable
    a->net_screen = NETS_CONFIRM_FORGET;
}

static void ftp_off(App *a) {
    ftpsrv_disable();
    ftpsrv_status(&a->ftp);
    if (a->net_screen == NETS_FTP) a->net_screen = NETS_MAIN;
    media_resume(a);                          // reload music/caches and pick up the new files
}

static void toggle_ftp(App *a) {
    if (a->ftp.phase != FTP_OFF) { ftp_off(a); return; }
    if (!a->net.switch_on) { ui_toast(a, tr(S_NET_ERR_SWITCH_OFF)); return; }
    media_suspend(a);
    ftpsrv_enable(a->net.phase == NET_CONNECTED);
    ftpsrv_status(&a->ftp);
    if (a->ftp.phase == FTP_ON) a->net_screen = NETS_FTP;
    else {      // pick a network first; the server starts on its own
        a->netnav.section = 0;                // Wi-Fi
        a->netnav.focus = NF_PANEL;
        ui_toast(a, tr(S_FTP_NEED_WIFI));
    }
}

// USB: the PC gets the memory stick, so nothing may read or write ms0: until it is back.
// The PSP keeps the FAT in its own cache: a write that is not flushed before the PC takes the
// card, or any write after it (the cache no longer matches the card), overwrites the PC's
// files (1.1.13, E1000: VBOOT.PBP cut to 32 KB). So: finish and flush every write first,
// then after USB restart Nemo instead of touching the card with a stale cache.
static void usb_open(App *a) {
    if (a->music_scan_state == 1) { ui_toast(a, tr(S_USB_WAIT_SCAN)); return; }   // that thread reads ms0:
    if (selfguard_busy()) { ui_toast(a, tr(S_USB_WAIT_BACKUP)); return; }
    if (files_worker_busy()) { ui_toast(a, tr(S_FM_USB_WAIT)); return; }   // 1.3.2: copying on the card
    media_suspend(a);                         // last write: the music state (synchronous)
    for (int i = 0; i < 300 && art_busy(); i++) SDL_Delay(10);   // an artwork cache write in flight
    photo_thumbs_pause(1);                    // 1.3.1: a photo thumbnail being cached must land first
    for (int i = 0; i < 300 && photo_thumbs_busy(); i++) { photo_thumbs_stop(); SDL_Delay(10); }
    selfguard_clean_exit();                   // the restart after USB is not a failed start
    log_msg("usb: on");
    platform_sync_card();                     // logged; the second one also flushes the closed log
    log_pause();
    platform_sync_card();
    if (usb_enter() != 0) {
        log_resume();
        media_resume(a);
        ui_fail(a, S_USB_FAILED);
        return;
    }
    a->net_screen = NETS_USB;
    idle_reset(&a->idle, a->now_ms);
}

// Back from USB: the PSP's view of the card is stale, so restart (fresh FAT cache, new files
// picked up) without writing anything first. Only if that fails, carry on as before.
static void usb_restart(App *a) {
    launch_self();
    log_resume();
    log_msg("usb: restart failed, rescanning");
    a->net_screen = NETS_MAIN;
    media_resume(a);
}

static void usb_close(App *a) {
    usb_leave();
    usb_restart(a);
}

void network_on_suspend(App *a) {
    if (a->ftp.phase != FTP_OFF) ftpsrv_disable();
    if (usb_active()) usb_leave();            // restart on wake (network_on_wake)
}

void network_on_wake(App *a) {
    ftpsrv_status(&a->ftp);
    if (a->net_screen == NETS_USB && !usb_active()) { usb_restart(a); return; }
    if (a->ftp.phase == FTP_OFF && a->net_screen != NETS_USB) media_resume(a);
}

void network_toggle_wpa2(App *a) {
    int next = !a->wpa2_on;
    int rc = platform_ark_set("wpa2", next, 1);
    log_msg("network: wpa2 -> %d rc=%d", next, rc);
    if (rc == 0) {
        a->wpa2_on = next;
        ui_toast(a, tr(S_NET_WPA2_RESTART));
    } else {
        ui_toast(a, tr(S_ERR_SETTINGS_WRITE));
    }
}

// Why the keyboard refused to finish: the length rule of the network's security type.
static void password_rule_toast(App *a) {
    ui_toast(a, tr(net_sec_kind(a->net_target.sec) == NSEC_WEP ? S_NET_PW_RULE_WEP : S_NET_PW_RULE_WPA));
}

static void osk_input(App *a, Action act) {
    int finish_try = act == ACT_START ||
                     (act == ACT_CONFIRM && strcmp(osk_key(&a->osk, a->osk.row, a->osk.col), "DONE") == 0);
    OskState st = osk_handle(&a->osk, act);
    if (st == OSK_CANCEL) { a->net_screen = NETS_MAIN; return; }
    if (st != OSK_DONE) {
        if (finish_try) password_rule_toast(a);
        return;
    }
    if (!net_password_ok(net_sec_kind(a->net_target.sec), a->osk.text)) { password_rule_toast(a); return; }
    a->net_target_pw = 1;
    net_connect_ap(&a->net_target, a->osk.text);
    memset(a->osk.text, 0, sizeof a->osk.text);
    a->net_screen = NETS_MAIN;
}

static int confirm_input(App *a, Action act) {   // 1 = yes chosen
    switch (act) {
    case ACT_LEFT: case ACT_RIGHT: a->net_confirm_sel = !a->net_confirm_sel; break;
    case ACT_BACK: a->net_screen = NETS_MAIN; break;
    case ACT_CONFIRM:
        a->net_screen = NETS_MAIN;
        return a->net_confirm_sel == 0;
    default: break;
    }
    return 0;
}

void network_action(App *a, Action act) {
    switch (a->net_screen) {
    case NETS_OSK: osk_input(a, act); return;
    case NETS_MENU:
        if (act == ACT_UP || act == ACT_DOWN) a->net_menu_sel = !a->net_menu_sel;
        else if (act == ACT_BACK) a->net_screen = NETS_MAIN;
        else if (act == ACT_CONFIRM) {
            a->net_screen = NETS_MAIN;
            if (a->net_menu_sel == 0) net_disconnect();
            else if (a->net_slot > 0) { a->net_confirm_sel = 1; a->net_screen = NETS_CONFIRM_FORGET; }
        }
        return;
    case NETS_CONFIRM_FORGET:
        if (confirm_input(a, act) && a->net_slot > 0) net_forget(a->net_slot);   // disconnects first if needed
        return;
    case NETS_CONFIRM_RESCAN:
        if (confirm_input(a, act)) net_disconnect_and_scan();
        return;
    case NETS_USB:
        if (act == ACT_BACK) usb_close(a);
        return;
    case NETS_FTP:
        if (act == ACT_BACK) ftp_off(a);
        return;
    default: break;
    }
    // Main view: connecting / error states take ✕ and ○ first.
    if (wifi_section(a) && a->netnav.focus == NF_PANEL) {
        if (a->net.phase == NET_CONNECTING) {
            if (act == ACT_BACK) net_cancel();
            return;
        }
        if (a->net.phase == NET_ERROR) {
            if (act == ACT_CONFIRM && a->net.error == S_NET_ERR_AUTH && a->net_target_pw) open_password(a, &a->net_target);
            else if (act == ACT_BACK || act == ACT_CONFIRM) net_clear_error();
            return;
        }
    }
    NetNavModel m = { wifi_section(a) ? a->net_row_count : 0 };
    NetCmd c = netnav_handle(&a->netnav, act, &m);
    switch (c.type) {
    case NC_ROW: connect_row(a, c.index); break;
    case NC_REFRESH:
        if (!a->net.switch_on) ui_toast(a, tr(S_NET_ERR_SWITCH_OFF));
        else if (a->net.phase == NET_CONNECTED && a->net.scan_refused) {
            a->net_confirm_sel = 1;
            a->net_screen = NETS_CONFIRM_RESCAN;
        } else net_scan();
        break;
    case NC_FORGET: ask_forget(a, c.index); break;
    case NC_WPA2_TOGGLE: network_toggle_wpa2(a); break;
    case NC_FTP_TOGGLE: toggle_ftp(a); break;
    case NC_USB_OPEN: usb_open(a); break;
    case NC_BACK_TO_TAB: break;  // stays on the tab (same as other tabs' root)
    default: break;
    }
}

void network_draw_hints(const App *a) {
    switch (a->net_screen) {
    case NETS_OSK: {
        static const Hint h[] = { { ACT_CONFIRM, S_OSK_HINT_TYPE }, { ACT_QUICK, S_OSK_HINT_DEL },
                                  { ACT_MENU, S_HINT_SHOW_PW }, { ACT_TAB_PREV, S_OSK_HINT_CAPS },
                                  { ACT_START, S_OSK_HINT_DONE } };
        ui_hints_draw(a, h, 5);
        return;
    }
    case NETS_FTP: {
        static const Hint h[] = { { ACT_BACK, S_FTP_TURN_OFF } };
        ui_hints_draw(a, h, 1);
        return;
    }
    case NETS_USB: {
        static const Hint h[] = { { ACT_BACK, S_USB_EXIT } };
        ui_hints_draw(a, h, 1);
        return;
    }
    case NETS_MENU: case NETS_CONFIRM_FORGET: case NETS_CONFIRM_RESCAN: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
        return;
    }
    default: break;
    }
    if (a->netnav.focus == NF_COLUMN) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT } };
        ui_hints_draw(a, h, 1);
        return;
    }
    switch (netnav_current(&a->netnav)) {
    case NS_WIFI:
        if (a->net.phase == NET_CONNECTING) {
            static const Hint h[] = { { ACT_BACK, S_HINT_CANCEL } };
            ui_hints_draw(a, h, 1);
        } else if (a->net.phase == NET_ERROR) {
            Hint h[2] = { { ACT_CONFIRM, S_HINT_RETRY }, { ACT_BACK, S_HINT_BACK } };
            int retry = a->net.error == S_NET_ERR_AUTH && a->net_target_pw;
            ui_hints_draw(a, retry ? h : h + 1, retry ? 2 : 1);
        } else {
            static const Hint h[] = { { ACT_CONFIRM, S_HINT_CONNECT }, { ACT_QUICK, S_HINT_REFRESH },
                                      { ACT_MENU, S_HINT_FORGET }, { ACT_LEFT, S_HINT_SECTIONS } };
            ui_hints_draw(a, h, 4);
        }
        break;
    default: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_TOGGLE }, { ACT_LEFT, S_HINT_SECTIONS } };
        ui_hints_draw(a, h, 2);
        break;
    }
    }
}
