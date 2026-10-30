#include "umd.h"
#include "log.h"
#include "sfo.h"
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspumd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define POLL_US 500000
#define READY_TIMEOUT_US 5000000

static volatile int ev_ready;
static UmdEvent ev, cur;
static volatile int cur_valid;
static SceUID lock = -1;

static int dir_exists(const char *p) {
    SceUID d = sceIoDopen(p);
    if (d < 0) return 0;
    sceIoDclose(d);
    return 1;
}

static void post(const UmdEvent *e) {
    sceKernelWaitSema(lock, 1, NULL);
    ev = *e;
    ev_ready = 1;
    sceKernelSignalSema(lock, 1);
}

static void read_sfo(const char *path, UmdEvent *e) {
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("umd: no %s", path); return; }
    uint8_t *b = malloc(16 * 1024);
    size_t n = b ? fread(b, 1, 16 * 1024, f) : 0;
    fclose(f);
    if (b) {
        if (sfo_get_string(b, n, "TITLE", e->title, sizeof e->title) != 0) e->title[0] = 0;
        if (sfo_get_string(b, n, "DISC_ID", e->disc_id, sizeof e->disc_id) != 0) e->disc_id[0] = 0;
        char cat[16];
        if (sfo_get_string(b, n, "CATEGORY", cat, sizeof cat) == 0) log_msg("umd: %s category %s", path, cat);
    }
    free(b);
}

// A folder of the disc into the log (a layout never seen yet); streams = 1: its *.MPS go into e (UMD Music).
static void log_tree(const char *dir, int streams, UmdEvent *e) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) { log_msg("umd: %s: %08x", dir, (unsigned)d); return; }
    SceIoDirent de;
    memset(&de, 0, sizeof de);
    int n = 0;
    while (sceIoDread(d, &de) > 0) {
        if (n++ < 200) log_msg("umd: %s/%s %s %lld B", dir, de.d_name, FIO_S_ISDIR(de.d_stat.st_mode) ? "dir" : "file",
                               (long long)de.d_stat.st_size);
        size_t l = strlen(de.d_name);
        if (streams && e && e->n_streams < UMD_MAX_STREAMS && l > 4 && l < sizeof e->streams[0] &&
            !strcasecmp(de.d_name + l - 4, ".MPS"))
            snprintf(e->streams[e->n_streams++], sizeof e->streams[0], "%s", de.d_name);
        memset(&de, 0, sizeof de);
    }
    sceIoDclose(d);
}

// The first bytes of a file of unknown layout, as hex.
static void log_head(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("umd: no %s", path); return; }
    uint8_t b[256];
    int n = (int)fread(b, 1, sizeof b, f);
    fclose(f);
    for (int i = 0; i < n; i += 16) {
        char hex[16 * 3 + 1];
        int k = 0;
        for (int j = i; j < n && j < i + 16; j++) k += snprintf(hex + k, sizeof hex - (size_t)k, "%02x ", b[j]);
        log_msg("umd: %s +%03x: %s", path, i, hex);
    }
}

// The disc that just came in: kind, and for a game its title and ID.
static void read_disc(UmdEvent *e) {
    memset(e, 0, sizeof *e);
    e->kind = UMD_EV_OTHER;
    if (dir_exists("disc0:/PSP_GAME")) {
        e->kind = UMD_EV_GAME;
        FILE *f = fopen("disc0:/PSP_GAME/PARAM.SFO", "rb");
        if (f) {
            uint8_t *b = malloc(16 * 1024);
            size_t n = b ? fread(b, 1, 16 * 1024, f) : 0;
            fclose(f);
            if (b) {
                if (sfo_get_string(b, n, "TITLE", e->title, sizeof e->title) != 0) e->title[0] = 0;
                if (sfo_get_string(b, n, "DISC_ID", e->disc_id, sizeof e->disc_id) != 0) e->disc_id[0] = 0;
            }
            free(b);
        }
        if (!e->title[0]) snprintf(e->title, sizeof e->title, "UMD");
    } else if (dir_exists("disc0:/UMD_VIDEO")) {   // 1.3.3: played by Nemo's player; its title and ID
        e->kind = UMD_EV_VIDEO;
        read_sfo("disc0:/UMD_VIDEO/PARAM.SFO", e);
        if (!e->title[0]) snprintf(e->title, sizeof e->title, "UMD Video");
        if (!e->disc_id[0]) snprintf(e->disc_id, sizeof e->disc_id, "VIDEO");
    } else if (dir_exists("disc0:/UMD_AUDIO")) {   // 1.3.5: an album in Music
        e->kind = UMD_EV_MUSIC;
        read_sfo("disc0:/UMD_AUDIO/PARAM.SFO", e);
        if (!e->title[0]) snprintf(e->title, sizeof e->title, "UMD Music");
        if (!e->disc_id[0]) snprintf(e->disc_id, sizeof e->disc_id, "MUSIC");
        log_tree("disc0:/UMD_AUDIO", 0, NULL);
        log_tree("disc0:/UMD_AUDIO/STREAM", 1, e);
        log_head("disc0:/UMD_AUDIO/PLAYLIST.UMD");
    }
}

static int umd_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int known = 0, first = 1;
    for (;;) {
        int in = sceUmdCheckMedium() != 0;
        if (in && !known) {
            sceUmdActivate(1, "disc0:");
            int rc = sceUmdWaitDriveStatWithTimer(PSP_UMD_READY, READY_TIMEOUT_US);
            UmdEvent e;
            read_disc(&e);
            e.at_start = first;
            log_msg("umd: in, ready rc=%08x kind=%d id=%s \"%s\"", (unsigned)rc, e.kind, e.disc_id, e.title);
            if (e.kind == UMD_EV_GAME) {
                sceKernelWaitSema(lock, 1, NULL);
                cur = e;
                cur_valid = 1;
                sceKernelSignalSema(lock, 1);
            }
            post(&e);
            known = 1;
        } else if (!in && known) {
            UmdEvent e;
            memset(&e, 0, sizeof e);
            e.kind = UMD_EV_REMOVED;
            cur_valid = 0;
            log_msg("umd: out");
            post(&e);
            known = 0;
        }
        first = 0;
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

void umd_start(void) {
    lock = sceKernelCreateSema("nemo_umd", 0, 1, 1, NULL);
    SceUID th = sceKernelCreateThread("nemo_umd", umd_thread, 0x30, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    log_msg("umd: watch thread %08x", (unsigned)th);
}

int umd_take_event(UmdEvent *e) {
    if (!ev_ready || lock < 0) return 0;
    sceKernelWaitSema(lock, 1, NULL);
    *e = ev;
    ev_ready = 0;
    sceKernelSignalSema(lock, 1);
    return 1;
}

int umd_current(UmdEvent *e) {
    if (lock < 0) return 0;
    sceKernelWaitSema(lock, 1, NULL);
    int ok = cur_valid;
    if (ok) *e = cur;
    sceKernelSignalSema(lock, 1);
    return ok;
}
