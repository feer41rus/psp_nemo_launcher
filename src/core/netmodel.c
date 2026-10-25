#include "netmodel.h"
#include <string.h>

int net_ap_from_raw(NetAp *out, const uint8_t ssid[32], uint32_t ssid_len, const uint8_t bssid[6], int ch, int sig,
                    uint32_t sec) {
    memset(out, 0, sizeof *out);
    if (ssid_len > NET_SSID_MAX) ssid_len = NET_SSID_MAX;
    memcpy(out->ssid, ssid, ssid_len);
    out->ssid[ssid_len] = '\0';
    if (!out->ssid[0]) return 0;
    memcpy(out->bssid, bssid, 6);
    out->channel = ch;
    out->signal = sig;
    out->sec = sec;
    return 1;
}

int net_profile_for_ssid(const NetProfile *profs, int n, const char *ssid) {
    int best = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(profs[i].ssid, ssid) == 0 && (best < 0 || profs[i].slot < profs[best].slot)) best = i;
    return best;
}

int net_rows(const NetAp *aps, int ap_count, const NetProfile *profs, int prof_count, const char *connected_ssid,
             NetRow *out, int max) {
    int n = 0;
    if (!connected_ssid) connected_ssid = "";
    for (int i = 0; i < ap_count && n < max; i++) {
        int dup = -1;
        for (int j = 0; j < n; j++)
            if (strcmp(aps[out[j].ap].ssid, aps[i].ssid) == 0) dup = j;
        if (dup >= 0) {
            if (aps[i].signal > aps[out[dup].ap].signal) out[dup].ap = i;
            continue;
        }
        out[n].kind = NROW_AP;
        out[n].ap = i;
        n++;
    }
    for (int i = 1; i < n; i++)   // insertion sort by signal, strongest first
        for (int j = i; j > 0 && aps[out[j].ap].signal > aps[out[j - 1].ap].signal; j--) {
            NetRow t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    for (int i = 0; i < n; i++) {
        out[i].profile = net_profile_for_ssid(profs, prof_count, aps[out[i].ap].ssid);
        out[i].connected = connected_ssid[0] && strcmp(aps[out[i].ap].ssid, connected_ssid) == 0;
    }
    int near = n;
    for (int slot = 1; slot <= NET_MAX_PROFILES; slot++)
        for (int p = 0; p < prof_count && n < max; p++) {
            if (profs[p].slot != slot || net_profile_for_ssid(profs, prof_count, profs[p].ssid) != p) continue;
            int seen = 0;
            for (int r = 0; r < near; r++)
                if (strcmp(aps[out[r].ap].ssid, profs[p].ssid) == 0) seen = 1;
            if (seen) continue;
            out[n].kind = NROW_SAVED;
            out[n].ap = -1;
            out[n].profile = p;
            out[n].connected = connected_ssid[0] && strcmp(profs[p].ssid, connected_ssid) == 0;
            n++;
        }
    return n;
}

int net_free_slot(const int used_slots[], int n) {
    for (int slot = 1; slot <= NET_MAX_PROFILES; slot++) {
        int used = 0;
        for (int i = 0; i < n; i++) if (used_slots[i] == slot) used = 1;
        if (!used) return slot;
    }
    return -1;
}

NetSecKind net_sec_kind(uint32_t sec) {
    if (sec == 0) return NSEC_OPEN;
    if (sec == 1) return NSEC_WEP;
    return NSEC_WPA;
}

uint32_t net_profile_secure(uint32_t scan_sec) {
    switch (net_sec_kind(scan_sec)) {
    case NSEC_OPEN: return 0;
    case NSEC_WEP: return 1;
    default: return 4;   // WPA-PSK (AES): what the 3000 stores for a WPA2 router (spike, 2026-10-25)
    }
}

static int all_in(const char *s, int (*ok)(int)) {
    for (; *s; s++) if (!ok((unsigned char)*s)) return 0;
    return 1;
}
static int printable(int c) { return c >= 0x20 && c <= 0x7e; }
static int hexdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

int net_password_ok(NetSecKind k, const char *pw) {
    size_t n = strlen(pw);
    switch (k) {
    case NSEC_OPEN: return 1;
    case NSEC_WEP:
        if (n == 5 || n == 13) return all_in(pw, printable);
        if (n == 10 || n == 26) return all_in(pw, hexdigit);
        return 0;
    default: return n >= 8 && n <= 63 && all_in(pw, printable);
    }
}

int net_step(int apctl_state) {
    switch (apctl_state) {
    case 2: return 1;            // joining
    case 5: case 6: return 2;    // EAP auth / key exchange
    case 3: return 3;            // getting IP
    case 4: return 4;            // got IP
    default: return 0;           // scanning / disconnected
    }
}

StrId net_error_text(int apctl_error, int timed_out, int switch_off, int last_step) {
    // Codes are not documented; the step reached tells more. 0x80410106: the access point refused
    // the join with a wrong WPA2 password (author's PSP 3000, 2026-10-25).
    if (switch_off) return S_NET_ERR_SWITCH_OFF;
    if (last_step >= 2 || (uint32_t)apctl_error == 0x80410106u) return S_NET_ERR_AUTH;
    if (timed_out) return S_NET_ERR_TIMEOUT;
    return S_NET_ERR_NO_ANSWER;
}

void ftp_password(uint32_t seed, char out[7]) {
    static const char ALPHABET[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    uint32_t x = seed * 2654435761u + 0x9e3779b9u;
    for (int i = 0; i < 6; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        out[i] = ALPHABET[x % (sizeof ALPHABET - 1)];
    }
    out[6] = '\0';
}
