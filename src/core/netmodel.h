#ifndef NEMO_NETMODEL_H
#define NEMO_NETMODEL_H

#include <stdint.h>
#include "i18n.h"

// Pure model of the Wi-Fi section: scan results, saved profiles, password rules, errors.
#define NET_SSID_MAX 32
#define NET_MAX_APS 32
#define NET_MAX_PROFILES 10

typedef enum { NSEC_OPEN, NSEC_WEP, NSEC_WPA } NetSecKind;

typedef struct {
    char ssid[NET_SSID_MAX + 1];
    uint8_t bssid[6];
    int channel, signal;   // signal in %
    uint32_t sec;          // scan security code (0 open, 1 WEP, 4 WPA/WPA2 seen on a 3000)
} NetAp;

typedef struct {
    int slot;              // system profile slot 1..10
    char name[65];
    char ssid[NET_SSID_MAX + 1];
    uint32_t secure;       // PSP_NETPARAM_SECURE
} NetProfile;

typedef enum { NROW_AP, NROW_SAVED } NetRowKind;

typedef struct {
    NetRowKind kind;
    int ap;                // index into aps (NROW_AP), -1 otherwise
    int profile;           // index into profs, -1 if none
    int connected;
} NetRow;

// Raw scan entry -> NetAp. SSID length is clamped to 32 and NUL-terminated. Empty SSID -> 0 (skip).
int net_ap_from_raw(NetAp *out, const uint8_t ssid[32], uint32_t ssid_len, const uint8_t bssid[6], int ch, int sig,
                    uint32_t sec);
// Rows of the Wi-Fi panel: nearby networks (same SSID merged, strongest kept, sorted by signal desc),
// then saved profiles that are not nearby (by slot). Returns the row count.
int net_rows(const NetAp *aps, int ap_count, const NetProfile *profs, int prof_count, const char *connected_ssid,
             NetRow *out, int max);
// Profile for an SSID: the one with the lowest slot, -1 if none.
int net_profile_for_ssid(const NetProfile *profs, int n, const char *ssid);
// First slot 1..10 not in used_slots, -1 when all are taken.
int net_free_slot(const int used_slots[], int n);
NetSecKind net_sec_kind(uint32_t sec);
uint32_t net_profile_secure(uint32_t scan_sec);
// WPA: 8..63 printable ASCII; WEP: 5/13 printable or 10/26 hex; open: anything (unused).
int net_password_ok(NetSecKind k, const char *pw);
// apctl state -> connect step shown to the user: 0 search, 1 joining, 2 key exchange, 3 getting IP, 4 done.
int net_step(int apctl_state);
// Failure text. last_step: net_step() reached before the failure (key exchange or later -> wrong password).
StrId net_error_text(int apctl_error, int timed_out, int switch_off, int last_step);
// FTP password: 6 characters without look-alikes (0 O 1 l I).
void ftp_password(uint32_t seed, char out[7]);

#endif
