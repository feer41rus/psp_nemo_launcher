#include "net.h"
#include "log.h"
#include "platform.h"
#include <pspkernel.h>
#include <psputility.h>
#include <psputility_netparam.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <pspnet_apctl.h>
#include <pspwlan.h>
#include <stdio.h>
#include <string.h>

// Scan API, not declared by PSPSDK (imports in net_imports.S).
int sceNetApctlScanUser(void);
int sceNetApctlGetBSSDescIDListUser(unsigned int *size, void *buf);
int sceNetApctlGetBSSDescEntryUser(int entry, int info, void *out);

#define CONNECT_TIMEOUT_MS 20000
#define SCAN_TIMEOUT_MS 10000
#define POLL_US 300000

typedef enum { C_NONE, C_SCAN, C_CONNECT_AP, C_CONNECT_SLOT, C_DISCONNECT, C_DISCONNECT_SCAN, C_FORGET } CmdType;

static SceUID thread = -1, wake = -1, lock = -1;
static volatile int started, quit, inited;
static NetSnapshot S;                          // guarded by lock
static struct { CmdType type; NetAp ap; char pw[64]; int has_pw; int slot; } pending;
static volatile uint32_t cancel_gen;
static volatile int ev_event = -1, ev_error;   // last apctl handler event

static void L(void) { sceKernelWaitSema(lock, 1, NULL); }
static void U(void) { sceKernelSignalSema(lock, 1); }

static void handler(int old, int new_, int event, int error, void *arg) {
    (void)arg;
    ev_event = event;
    if (error) ev_error = error;
    log_msg("net: apctl %d -> %d event=%d error=%08X", old, new_, event, (unsigned)error);
}

static int apctl_state(void) {
    int st = 0;
    if (sceNetApctlGetState(&st) < 0) return 0;
    return st;
}

static void set_phase(NetPhase p) { L(); S.phase = p; U(); }

static void read_profiles(void) {
    NetProfile p[NET_MAX_PROFILES];
    int n = 0;
    for (int slot = 1; slot <= NET_MAX_PROFILES; slot++) {
        if (sceUtilityCheckNetParam(slot) != 0) continue;
        netData d;
        memset(&p[n], 0, sizeof p[n]);
        p[n].slot = slot;
        memset(&d, 0, sizeof d);
        if (sceUtilityGetNetParam(slot, PSP_NETPARAM_NAME, &d) == 0) snprintf(p[n].name, sizeof p[n].name, "%.64s", d.asString);
        memset(&d, 0, sizeof d);
        if (sceUtilityGetNetParam(slot, PSP_NETPARAM_SSID, &d) == 0) snprintf(p[n].ssid, sizeof p[n].ssid, "%.32s", d.asString);
        memset(&d, 0, sizeof d);
        if (sceUtilityGetNetParam(slot, PSP_NETPARAM_SECURE, &d) == 0) p[n].secure = d.asUint;
        n++;
    }
    L();
    memcpy(S.profs, p, sizeof p);
    S.prof_count = n;
    U();
}

static int init_net(void) {
    int r;
    if ((r = sceUtilityLoadModule(PSP_MODULE_NET_COMMON)) < 0) log_msg("net: load common %08X", (unsigned)r);
    if ((r = sceUtilityLoadModule(PSP_MODULE_NET_INET)) < 0) log_msg("net: load inet %08X", (unsigned)r);
    if ((r = sceNetInit(256 * 1024, 42, 0, 42, 0)) < 0) { log_msg("net: sceNetInit %08X", (unsigned)r); return r; }
    if ((r = sceNetInetInit()) < 0) { log_msg("net: sceNetInetInit %08X", (unsigned)r); return r; }
    if ((r = sceNetResolverInit()) < 0) log_msg("net: sceNetResolverInit %08X", (unsigned)r);
    if ((r = sceNetApctlInit(10 * 1024, 48)) < 0) { log_msg("net: sceNetApctlInit %08X", (unsigned)r); return r; }
    sceNetApctlAddHandler(handler, NULL);
    inited = 1;
    platform_log_memory("net");
    return 0;
}

static void poll_status(void) {
    static uint32_t info_ms;
    int sw = sceWlanGetSwitchState() == 1;
    int st = apctl_state();
    uint32_t now = platform_ms();
    char ssid[NET_SSID_MAX + 1] = "", ip[16] = "";
    int sig = -1, ch = -1;
    if (st == PSP_NET_APCTL_STATE_GOT_IP && (now - info_ms > 2000 || !S.ip[0])) {
        SceNetApctlInfo info;
        if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID, &info) == 0) strncpy(ssid, (char *)info.ssid, NET_SSID_MAX);
        if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0) snprintf(ip, sizeof ip, "%.15s", info.ip);
        if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_STRENGTH, &info) == 0) sig = info.strength;
        if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_CHANNEL, &info) == 0) ch = info.channel;
        info_ms = now;
    }
    L();
    S.switch_on = sw;
    S.apctl_state = st;
    if (st == PSP_NET_APCTL_STATE_GOT_IP) {
        if (ssid[0]) strcpy(S.ssid, ssid);
        if (ip[0]) strcpy(S.ip, ip);
        if (sig >= 0) S.signal = sig;
        if (ch >= 0) S.channel = ch;
        if (S.phase == NET_IDLE || S.phase == NET_STARTING) S.phase = NET_CONNECTED;
    } else {
        if (S.phase == NET_CONNECTED) {
            log_msg("net: connection lost (state %d, switch %d)", st, sw);
            S.phase = NET_IDLE;
        }
        S.ssid[0] = S.ip[0] = '\0';
        if (S.phase == NET_STARTING) S.phase = NET_IDLE;
    }
    U();
}

static int wait_disconnected(int ms) {
    for (int t = 0; t < ms; t += 100) {
        if (apctl_state() == PSP_NET_APCTL_STATE_DISCONNECTED) return 1;
        sceKernelDelayThread(100 * 1000);
    }
    return 0;
}

static void run_scan(void) {
    int was_connected = apctl_state() == PSP_NET_APCTL_STATE_GOT_IP;
    set_phase(NET_SCANNING);
    ev_event = -1;
    int r = sceNetApctlScanUser();
    if (r < 0) {
        log_msg("net: scan refused %08X (connected=%d)", (unsigned)r, was_connected);
        L();
        S.scan_refused = was_connected;
        S.phase = was_connected ? NET_CONNECTED : NET_IDLE;
        U();
        return;
    }
    uint32_t t0 = platform_ms();
    sceKernelDelayThread(200 * 1000);
    while (platform_ms() - t0 < SCAN_TIMEOUT_MS && ev_event != PSP_NET_APCTL_EVENT_SCAN_COMPLETE &&
           apctl_state() == PSP_NET_APCTL_STATE_SCANNING)
        sceKernelDelayThread(100 * 1000);
    static unsigned char buf[8 * NET_MAX_APS];
    unsigned int size = sizeof buf;
    NetAp aps[NET_MAX_APS];
    int n = 0;
    r = sceNetApctlGetBSSDescIDListUser(&size, buf);
    if (r >= 0) {
        int count = (int)(size / 8);
        if (count > NET_MAX_APS) count = NET_MAX_APS;
        for (int i = 0; i < count; i++) {
            int id = *(int *)(buf + i * 8 + 4);
            uint8_t ssid[32] = { 0 }, bssid[6] = { 0 };
            unsigned int slen = 0, sec = 0;
            unsigned char ch = 0, sig = 0;
            sceNetApctlGetBSSDescEntryUser(id, 0, bssid);
            sceNetApctlGetBSSDescEntryUser(id, 1, ssid);
            sceNetApctlGetBSSDescEntryUser(id, 2, &slen);
            sceNetApctlGetBSSDescEntryUser(id, 3, &ch);
            sceNetApctlGetBSSDescEntryUser(id, 4, &sig);
            sceNetApctlGetBSSDescEntryUser(id, 5, &sec);
            if (net_ap_from_raw(&aps[n], ssid, slen, bssid, ch, sig, sec)) n++;
        }
    }
    log_msg("net: scan done r=%08X, %d networks in %u ms", (unsigned)r, n, (unsigned)(platform_ms() - t0));
    log_msg("time: wifi_scan %u ms", (unsigned)(platform_ms() - t0));   // net thread: plain log_msg
    L();
    if (r >= 0) {
        memcpy(S.aps, aps, sizeof aps);
        S.ap_count = n;
        S.scan_ms = platform_ms();
        S.scan_refused = 0;
    }
    S.phase = NET_IDLE;
    U();
}

// 1 connected, 0 failed (S.error set), -1 cancelled.
static int run_connect_slot(int slot, const char *ssid) {
    uint32_t gen = cancel_gen;
    if (apctl_state() != PSP_NET_APCTL_STATE_DISCONNECTED) {
        sceNetApctlDisconnect();
        wait_disconnected(3000);
    }
    L();
    S.phase = NET_CONNECTING;
    S.step = 0;
    strncpy(S.target, ssid, NET_SSID_MAX);
    S.target[NET_SSID_MAX] = '\0';
    U();
    ev_event = -1;
    ev_error = 0;
    int r = sceNetApctlConnect(slot);
    log_msg("net: connect slot %d \"%s\" = %08X", slot, ssid, (unsigned)r);
    uint32_t t0 = platform_ms();
    int step = 0, seen = 0, timed_out = 0, sw_off = 0;
    for (;;) {
        if (r < 0) break;
        int st = apctl_state();
        if (st != PSP_NET_APCTL_STATE_DISCONNECTED) seen = 1;
        int s = net_step(st);
        if (s > step) { step = s; L(); S.step = step; U(); }
        if (st == PSP_NET_APCTL_STATE_GOT_IP) {
            poll_status();
            L(); S.phase = NET_CONNECTED; U();
            log_msg("net: connected to \"%s\" in %u ms", ssid, (unsigned)(platform_ms() - t0));
            return 1;
        }
        if (cancel_gen != gen) {
            sceNetApctlDisconnect();
            wait_disconnected(3000);
            set_phase(NET_IDLE);
            log_msg("net: connect cancelled");
            return -1;
        }
        if (sceWlanGetSwitchState() != 1) { sw_off = 1; break; }
        if (ev_event == PSP_NET_APCTL_EVENT_ERROR) break;
        if (seen && st == PSP_NET_APCTL_STATE_DISCONNECTED && platform_ms() - t0 > 1500) break;
        if (platform_ms() - t0 > CONNECT_TIMEOUT_MS) { timed_out = 1; break; }
        sceKernelDelayThread(100 * 1000);
    }
    sceNetApctlDisconnect();
    wait_disconnected(3000);
    L();
    S.phase = NET_ERROR;
    S.error = net_error_text(ev_error, timed_out, sw_off, step);
    log_fail("wifi: %s (event error %08x, timed out %d, switch off %d, step %d)", tr_en(S.error), (unsigned)ev_error, timed_out, sw_off, step);
    U();
    log_msg("net: connect failed step=%d timeout=%d switch_off=%d error=%08X", step, timed_out, sw_off,
            (unsigned)ev_error);
    return 0;
}

static void set_error(StrId e) { L(); S.phase = NET_ERROR; S.error = e; U(); log_fail("wifi: %s", tr_en(e)); }

static void run_connect_ap(const NetAp *ap, const char *pw) {
    read_profiles();
    NetSnapshot s;
    net_snapshot(&s);
    int old = net_profile_for_ssid(s.profs, s.prof_count, ap->ssid);
    int old_slot = old >= 0 ? s.profs[old].slot : -1;
    if (!pw && old_slot > 0) {
        run_connect_slot(old_slot, ap->ssid);
        return;
    }
    int used[NET_MAX_PROFILES];
    for (int i = 0; i < s.prof_count; i++) used[i] = s.profs[i].slot;
    int slot = net_free_slot(used, s.prof_count);
    if (slot < 0) { set_error(S_NET_ERR_NO_SLOT); return; }
    NetSecKind kind = net_sec_kind(ap->sec);
    uint32_t secure = net_profile_secure(ap->sec);
    int r = sceUtilityCreateNetParam(slot);
    sceUtilitySetNetParam(PSP_NETPARAM_NAME, ap->ssid);
    sceUtilitySetNetParam(PSP_NETPARAM_SSID, ap->ssid);
    sceUtilitySetNetParam(PSP_NETPARAM_SECURE, &secure);
    if (kind == NSEC_WPA) {
        uint32_t one = 1;   // field 21 = 1 in profiles the XMB creates for WPA (spike)
        sceUtilitySetNetParam(21, &one);
        sceUtilitySetNetParam(22, pw ? pw : "");
    } else if (kind == NSEC_WEP) {
        sceUtilitySetNetParam(PSP_NETPARAM_WEPKEY, pw ? pw : "");
    }
    int c = sceUtilityCopyNetParam(0, slot);
    log_msg("net: new profile slot %d for \"%s\" secure=%u create=%08X copy=%08X", slot, ap->ssid, (unsigned)secure,
            (unsigned)r, (unsigned)c);
    read_profiles();
    if (r < 0 || c < 0) { set_error(S_NET_ERR_NO_ANSWER); return; }
    int ok = run_connect_slot(slot, ap->ssid);
    if (ok == 1) {
        if (old_slot > 0) {   // the password was re-entered: the new profile replaces the old one
            log_msg("net: replaced profile slot %d", old_slot);
            sceUtilityDeleteNetParam(old_slot);
        }
    } else {
        log_msg("net: removing the new profile slot %d", slot);
        sceUtilityDeleteNetParam(slot);
    }
    read_profiles();
}

static int thread_main(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    if (init_net() < 0) {
        platform_log_memory("net-fail");
        set_error(S_NET_ERR_NO_MEMORY);
        return 0;
    }
    read_profiles();
    poll_status();
    while (!quit) {
        SceUInt timeout = POLL_US;
        sceKernelWaitSema(wake, 1, &timeout);
        if (quit) break;
        L();
        CmdType type = pending.type;
        NetAp ap = pending.ap;
        char pw[64];
        memcpy(pw, pending.pw, sizeof pw);
        int has_pw = pending.has_pw, slot = pending.slot;
        pending.type = C_NONE;
        memset(pending.pw, 0, sizeof pending.pw);
        U();
        switch (type) {
        case C_SCAN: run_scan(); break;
        case C_CONNECT_AP: run_connect_ap(&ap, has_pw ? pw : NULL); break;
        case C_CONNECT_SLOT: {
            NetSnapshot s;
            net_snapshot(&s);
            const char *ssid = "";
            for (int i = 0; i < s.prof_count; i++) if (s.profs[i].slot == slot) ssid = s.profs[i].ssid;
            run_connect_slot(slot, ssid);
            break;
        }
        case C_DISCONNECT:
        case C_DISCONNECT_SCAN:
            sceNetApctlDisconnect();
            wait_disconnected(3000);
            set_phase(NET_IDLE);
            log_msg("net: disconnected by the user");
            if (type == C_DISCONNECT_SCAN) run_scan();
            break;
        case C_FORGET: {
            // Forgetting the network we are on also disconnects from it.
            NetSnapshot s;
            net_snapshot(&s);
            for (int i = 0; i < s.prof_count; i++)
                if (s.profs[i].slot == slot && s.phase == NET_CONNECTED && strcmp(s.profs[i].ssid, s.ssid) == 0) {
                    sceNetApctlDisconnect();
                    wait_disconnected(3000);
                    set_phase(NET_IDLE);
                    log_msg("net: disconnected before forgetting");
                }
            log_msg("net: forget slot %d = %08X", slot, (unsigned)sceUtilityDeleteNetParam(slot));
            read_profiles();
            break;
        }
        default: break;
        }
        memset(pw, 0, sizeof pw);
        poll_status();
    }
    return 0;
}

void net_start(void) {
    if (started) return;
    started = 1;
    lock = sceKernelCreateSema("nemo_net_lock", 0, 1, 1, NULL);
    wake = sceKernelCreateSema("nemo_net_wake", 0, 0, 1, NULL);
    memset(&S, 0, sizeof S);
    S.phase = NET_STARTING;
    thread = sceKernelCreateThread("nemo_net", thread_main, 0x30, 0x4000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_USBWLAN,
                                   NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
    log_msg("net: start thread=%08X", (unsigned)thread);
}

int net_started(void) { return started; }

static void post(CmdType type, const NetAp *ap, const char *pw, int slot) {
    if (!started) return;
    L();
    pending.type = type;
    if (ap) pending.ap = *ap;
    memset(pending.pw, 0, sizeof pending.pw);
    pending.has_pw = pw != NULL;
    if (pw) strncpy(pending.pw, pw, sizeof pending.pw - 1);
    pending.slot = slot;
    if (S.phase == NET_ERROR) S.phase = NET_IDLE;
    U();
    sceKernelSignalSema(wake, 1);
}

void net_scan(void) { post(C_SCAN, NULL, NULL, 0); }
void net_connect_ap(const NetAp *ap, const char *password) { post(C_CONNECT_AP, ap, password, 0); }
void net_connect_profile(int slot) { post(C_CONNECT_SLOT, NULL, NULL, slot); }
void net_disconnect(void) { post(C_DISCONNECT, NULL, NULL, 0); }
void net_disconnect_and_scan(void) { post(C_DISCONNECT_SCAN, NULL, NULL, 0); }
void net_forget(int slot) { post(C_FORGET, NULL, NULL, slot); }
void net_cancel(void) { cancel_gen++; }

void net_clear_error(void) {
    if (!started) return;
    L();
    if (S.phase == NET_ERROR) S.phase = NET_IDLE;
    U();
}

void net_snapshot(NetSnapshot *out) {
    if (!started) {
        memset(out, 0, sizeof *out);
        out->phase = NET_OFF;
        out->switch_on = sceWlanGetSwitchState() == 1;
        return;
    }
    L();
    *out = S;
    U();
    out->switch_on = sceWlanGetSwitchState() == 1;   // also right when the worker failed to start
}

void net_shutdown(void) {
    if (!started) return;
    quit = 1;
    cancel_gen++;
    sceKernelSignalSema(wake, 1);
    SceUInt timeout = 3000 * 1000;
    if (thread >= 0 && sceKernelWaitThreadEnd(thread, &timeout) < 0) sceKernelTerminateThread(thread);
    if (inited) {
        sceNetApctlDisconnect();
        sceNetApctlTerm();
        sceNetResolverTerm();
        sceNetInetTerm();
        sceNetTerm();
        sceUtilityUnloadModule(PSP_MODULE_NET_INET);
        sceUtilityUnloadModule(PSP_MODULE_NET_COMMON);
    }
    if (thread >= 0) sceKernelDeleteThread(thread);
    sceKernelDeleteSema(wake);
    sceKernelDeleteSema(lock);
    thread = wake = lock = -1;
    started = quit = inited = 0;   // a failed launch can start Wi-Fi again
    log_msg("net: shut down");
}

void net_on_resume(void) {
    if (!started) return;
    log_msg("net: resumed, state %d", apctl_state());
    sceKernelSignalSema(wake, 1);   // re-poll right away
}
