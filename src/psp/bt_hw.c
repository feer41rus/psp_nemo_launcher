// Bluetooth on the PSP Go (1.3.12). No public API: the registry is walked and Bluetooth-looking entries are
// matched by name (src/core/btreg.c). The detailed log lists what a PSP Go really has, so the guesses can be
// replaced by the real keys after the first console log.
#include "bt_hw.h"
#include "log.h"
#include "platform.h"
#include "sysreg.h"
#include <pspmodulemgr.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_ENTRIES 96

typedef struct {
    RegEntry e[MAX_ENTRIES];
    int n, dirs, keys, log_all;
} Walk;

static void on_item(void *ctx, const SysregItem *it) {
    Walk *w = ctx;
    if (it->kind == SYSREG_DIR) {
        w->dirs++;
        if (w->log_all) log_msg("bt: reg dir %s", it->path);   // every category: where Bluetooth might live
        return;
    }
    w->keys++;
    if (!btreg_is_bt_path(it->path)) return;
    if (w->log_all) {
        if (it->hidden) log_msg("bt: reg %s (hidden, %d bytes)", it->path, it->size);
        else if (it->kind == SYSREG_INT) log_msg("bt: reg %s = %d", it->path, it->ival);
        else if (it->kind == SYSREG_STR) log_msg("bt: reg %s = \"%s\"", it->path, it->sval);
        else {
            char hex[3 * 16 + 1] = { 0 };
            int n = it->size < 16 ? it->size : 16;
            for (int i = 0; i < n; i++) snprintf(hex + i * 3, 4, "%02x ", it->bin[i]);
            log_msg("bt: reg %s (binary %d bytes) %s", it->path, it->size, hex);
        }
    }
    if (it->hidden || w->n >= MAX_ENTRIES) return;
    RegEntry *e = &w->e[w->n++];
    snprintf(e->path, sizeof e->path, "%s", it->path);
    e->type = it->kind == SYSREG_INT ? REG_INT : it->kind == SYSREG_STR ? REG_STR : REG_BIN;
    e->ival = it->ival;
    snprintf(e->sval, sizeof e->sval, "%s", it->sval);
    e->size = it->size;
}

// Loaded modules whose names look like the Bluetooth stack: tells whether it runs at all under Nemo.
static void log_modules(void) {
    static SceUID ids[256];
    int count = 0, rc = sceKernelGetModuleIdList(ids, sizeof ids, &count);
    if (rc < 0) { log_msg("bt: module list failed %08x", (unsigned)rc); return; }
    if (count > 256) count = 256;
    int shown = 0, unreadable = 0;
    static const char *const W[] = { "bt", "blue", "hci", "l2cap", "rfcomm", "sdp", "a2dp", "hid", "headset" };
    for (int i = 0; i < count; i++) {
        SceKernelModuleInfo info;
        memset(&info, 0, sizeof info);
        info.size = sizeof info;
        if (sceKernelQueryModuleInfo(ids[i], &info) < 0) { unreadable++; continue; }
        int hit = 0;
        for (unsigned k = 0; k < sizeof W / sizeof *W && !hit; k++)
            for (const char *p = info.name; *p && !hit; p++)
                if (strncasecmp(p, W[k], strlen(W[k])) == 0) hit = 1;
        if (hit) { log_msg("bt: module \"%s\"", info.name); shown++; }
    }
    log_msg("bt: modules %d loaded, %d bluetooth-like, %d not readable from user mode", count, shown, unreadable);
}

void bt_log_state(const char *why, const BtState *s) {
    char paired[4 * 50] = { 0 };
    for (int i = 0; i < s->paired; i++) {
        strncat(paired, i ? ", " : "", sizeof paired - strlen(paired) - 1);
        strncat(paired, s->paired_names[i], sizeof paired - strlen(paired) - 1);
    }
    log_msg("bt: %s: found %d enabled %d connected %d \"%s\" paired %d [%s]", why, s->found, s->enabled,
            s->connected, s->connected_name, s->paired, paired);
}

void bt_refresh(BtState *out, int log_all) {
    static Walk w;
    memset(&w, 0, sizeof w);
    w.log_all = log_all;
    uint32_t t0 = platform_ms();
    if (log_all) log_msg("bt: ===== Bluetooth discovery (PSP Go) =====");
    sysreg_walk("/CONFIG", on_item, &w);
    btreg_scan(w.e, w.n, out);
    uint32_t dt = platform_ms() - t0;
    if (log_all || dt > 50) log_msg("bt: registry walk %d categories, %d keys, %d bluetooth-like, %u ms", w.dirs, w.keys, w.n, (unsigned)dt);
    if (log_all) {
        log_modules();
        bt_log_state("decision", out);
        log_msg("bt: ===== end of discovery =====");
    }
}
