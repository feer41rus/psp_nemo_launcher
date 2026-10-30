#include "btreg.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int has(const char *s, const char *word) {
    for (; *s; s++)
        if (strncasecmp(s, word, strlen(word)) == 0) return 1;
    return 0;
}

// One segment: "BT", "bt_enable", "BtDevice" (BT then a separator or an upper-case letter) or "...BLUETOOTH...".
static int bt_segment(const char *seg, size_t len) {
    char s[64];
    if (len >= sizeof s) len = sizeof s - 1;
    memcpy(s, seg, len);
    s[len] = '\0';
    if (has(s, "bluetooth")) return 1;
    if (strncasecmp(s, "bt", 2) != 0) return 0;
    char next = s[2];
    return next == '\0' || next == '_' || next == '-' || isdigit((unsigned char)next) ||
           (isupper((unsigned char)next) && s[0] == 'B' && s[1] == 't') || (s[0] == 'B' && s[1] == 'T');
}

int btreg_is_bt_path(const char *path) {
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        const char *e = p;
        while (*e && *e != '/') e++;
        if (e > p && bt_segment(p, (size_t)(e - p))) return 1;
        p = e;
    }
    return 0;
}

static const char *key_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

void btreg_scan(const RegEntry *e, int n, BtState *out) {
    memset(out, 0, sizeof *out);
    out->enabled = out->connected = -1;
    for (int i = 0; i < n; i++) {
        if (!btreg_is_bt_path(e[i].path)) continue;
        out->found = 1;
        const char *k = key_name(e[i].path);
        int connect = has(k, "connect") && !has(k, "auto");
        if (e[i].type == REG_INT) {
            if (connect && !has(k, "name")) out->connected = e[i].ival != 0;
            else if (has(k, "enable") || has(k, "power") || has(k, "onoff") || strcasecmp(k, "on") == 0)
                out->enabled = e[i].ival != 0;
        } else if (e[i].type == REG_STR && e[i].sval[0] && has(k, "name")) {
            if (connect) snprintf(out->connected_name, sizeof out->connected_name, "%s", e[i].sval);
            else if (out->paired < BT_MAX_PAIRED)
                snprintf(out->paired_names[out->paired++], sizeof out->paired_names[0], "%.47s", e[i].sval);
        }
    }
    if (out->connected_name[0] && out->connected < 0) out->connected = 1;
}
