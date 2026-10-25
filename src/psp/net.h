#ifndef NEMO_NET_H
#define NEMO_NET_H

#include <stdint.h>
#include "netmodel.h"

// Wi-Fi on a worker thread: modules are loaded on first use (net_start), every command runs on the
// thread, the UI reads a snapshot every frame. Never call on a console without Wi-Fi.
typedef enum { NET_OFF, NET_STARTING, NET_IDLE, NET_SCANNING, NET_CONNECTING, NET_CONNECTED, NET_ERROR } NetPhase;

typedef struct {
    NetPhase phase;
    int switch_on;                 // WLAN switch
    int apctl_state;
    int step;                      // connect: furthest net_step() reached
    char target[NET_SSID_MAX + 1]; // network being connected
    char ssid[NET_SSID_MAX + 1];   // connected network
    char ip[16];
    int signal, channel;
    StrId error;                   // valid in NET_ERROR
    NetAp aps[NET_MAX_APS];
    int ap_count;
    uint32_t scan_ms;              // platform_ms() of the last successful scan, 0 = never
    int scan_refused;              // last scan was refused while connected (list is old)
    NetProfile profs[NET_MAX_PROFILES];
    int prof_count;
} NetSnapshot;

void net_start(void);                                        // idempotent
int  net_started(void);
void net_scan(void);
void net_connect_ap(const NetAp *ap, const char *password);  // NULL password: saved profile / open network
void net_connect_profile(int slot);
void net_disconnect(void);
void net_disconnect_and_scan(void);
void net_forget(int slot);
void net_cancel(void);
void net_clear_error(void);
void net_snapshot(NetSnapshot *out);
void net_shutdown(void);                                     // before launching a game / leaving Nemo
void net_on_resume(void);                                    // after the PSP woke up

#endif
