// Memory screens (1.2.6): card/flash space by devctl, the boot sector, the UMD image size and the
// category count. The count walks only the folders of the categories, with sceIoDread (sizes come
// with the entries), yields often so music keeps playing, and keeps the last full result until exit.
#include "memory_hw.h"
#include "log.h"
#include "platform.h"
#include "umd.h"
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define DEVCTL_GET_INFO 0x02425818
#define SCAN_DEPTH 16
#define SCAN_YIELD_EVERY 64
#define SCAN_PRIO 0x50          // below music decoding and the main loop
#define SCAN_STACK 0x8000

typedef struct { uint32_t max_clusters, free_clusters, max_sectors, sector_size, sectors_per_cluster; } DevInfo;

int memory_card_read(const char *dev, MemCard *c) {
    DevInfo info;
    DevInfo *p = &info;
    memset(c, 0, sizeof *c);
    memset(&info, 0, sizeof info);
    static int logged;
    uint32_t t0 = platform_ms();
    int rc = sceIoDevctl(dev, DEVCTL_GET_INFO, &p, sizeof p, NULL, 0);
    uint32_t dt = platform_ms() - t0;
    if (!logged || dt > 100) log_msg("mem: devctl %s rc=%08x %u ms", dev, (unsigned)rc, (unsigned)dt);   // main thread: watch for stalls
    logged = 1;
    if (rc < 0 || !info.max_clusters || !info.sector_size || !info.sectors_per_cluster) return -1;
    c->cluster = info.sector_size * info.sectors_per_cluster;
    c->clusters = info.max_clusters;
    c->total = (long long)info.max_clusters * c->cluster;
    c->free = (long long)info.free_clusters * c->cluster;
    c->ok = 1;
    return 0;
}

static int read_sector(const char *dev, long long offset, uint8_t sec[512]) {
    SceUID fd = sceIoOpen(dev, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    int ok = (offset == 0 || sceIoLseek(fd, offset, PSP_SEEK_SET) == offset) && sceIoRead(fd, sec, 512) == 512;
    sceIoClose(fd);
    return ok ? 0 : -1;
}

int memory_card_boot(MemFs *fs, char label[12]) {
    static uint8_t sec[512];
    if (read_sector("msstor0p1:", 0, sec) == 0 && memstat_parse_boot(sec, fs, label) == 0) {
        log_msg("mem: boot sector via msstor0p1: %s", memstat_fs_name(*fs));
        return 0;
    }
    // Whole device: LBA 0 is the MBR; the first partition starts at entry 1 (offset 0x1C6).
    if (read_sector("msstor0:", 0, sec) == 0 && sec[510] == 0x55 && sec[511] == 0xAA) {
        uint32_t lba = (uint32_t)sec[0x1C6] | (uint32_t)sec[0x1C7] << 8 | (uint32_t)sec[0x1C8] << 16 |
                       (uint32_t)sec[0x1C9] << 24;
        if (lba && read_sector("msstor0:", (long long)lba * 512, sec) == 0 && memstat_parse_boot(sec, fs, label) == 0) {
            log_msg("mem: boot sector via msstor0: lba %u %s", (unsigned)lba, memstat_fs_name(*fs));
            return 0;
        }
    }
    log_msg("mem: boot sector unreadable");
    return -1;
}

long long memory_umd_size(void) {
    UmdEvent e;
    static char last_id[16];
    static long long last_size = -1;
    if (!umd_current(&e) || e.kind != UMD_EV_GAME) return -1;
    if (last_id[0] && strcmp(last_id, e.disc_id) == 0) return last_size;   // the drive is read once per disc
    static uint8_t pvd[2048];
    uint32_t t0 = platform_ms();
    SceUID fd = sceIoOpen("disc0:/sce_lbn0x10_size0x800", PSP_O_RDONLY, 0);   // sector 16: primary volume descriptor
    if (fd < 0) return -1;
    int n = sceIoRead(fd, pvd, sizeof pvd);
    sceIoClose(fd);
    if (n != (int)sizeof pvd || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return -1;
    uint32_t blocks = (uint32_t)pvd[80] | (uint32_t)pvd[81] << 8 | (uint32_t)pvd[82] << 16 | (uint32_t)pvd[83] << 24;
    snprintf(last_id, sizeof last_id, "%s", e.disc_id);
    last_size = (long long)blocks * 2048;
    log_msg("mem: umd %s %lld bytes, %u ms", e.disc_id, last_size, (unsigned)(platform_ms() - t0));
    return last_size;
}

// ---- category count ----

static SceUID lock = -1;
// Slot 0 = the memory card (ms0:), 1 = a PSP Go's internal storage (ef0:); one count at a time.
static volatile int running, stop_req, have_done[2], slot;
static MemTotals work, done[2];
static char cur_dir[64];
static uint32_t cluster;
static int entries;

// Kept out of walk(): its buffers would otherwise sit in every level's stack frame.
static __attribute__((noinline)) int has_eboot(const char *dir) {
    char p[560];
    SceIoStat st;
    snprintf(p, sizeof p, "%s/EBOOT.PBP", dir);
    return sceIoGetstat(p, &st) >= 0;
}

static volatile int read_error;

static void yield_now(void) {
    if (++entries % SCAN_YIELD_EVERY == 0) sceKernelDelayThread(1000);
}

static int slot_of(const char *dev) { return strncasecmp(dev, "ef0", 3) == 0; }
static const char *dev_of(int s) { return s ? "ef0:" : "ms0:"; }

// `path` holds "ms0:/<rel>" (or "ef0:/<rel>") and has room for 512 bytes; `rel` points into it after "ms0:/".
static void walk(char *path, const char *rel, int depth, int game_has_eboot) {
    if (depth > SCAN_DEPTH || stop_req) return;
    SceUID d = sceIoDopen(path);
    if (d < 0) return;
    size_t len = strlen(path);
    int game_root = depth == 0 && strcasecmp(rel, "PSP/GAME") == 0;   // children are games or DLC folders
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    int rc = 0;
    while (!stop_req && (rc = sceIoDread(d, &e)) > 0) {
        if (strcmp(e.d_name, ".") && strcmp(e.d_name, "..")) {
            if (len + 1 + strlen(e.d_name) < 512) {   // a longer path is left to "Other"
                path[len] = '/';
                strcpy(path + len + 1, e.d_name);
                if (FIO_S_ISDIR(e.d_stat.st_mode)) {
                    int eboot = game_has_eboot;
                    if (game_root) eboot = has_eboot(path);
                    walk(path, rel, depth + 1, eboot);
                } else {
                    MemCat c = memstat_category(rel, game_has_eboot);
                    sceKernelWaitSema(lock, 1, NULL);
                    memstat_add(&work, c, (long long)e.d_stat.st_size, cluster);
                    sceKernelSignalSema(lock, 1);
                }
                path[len] = '\0';
            }
            yield_now();
        }
        memset(&e, 0, sizeof e);
    }
    if (rc < 0) read_error = 1;   // stale handle (sleep) or card error: this count is not complete
    sceIoDclose(d);
}

static int scan_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    static const char *const ROOTS[] = { "ISO", "PSP/GAME", "PSP/SAVEDATA", "PSP/NEMO", "SEPLUGINS",
                                         "MUSIC", "PICTURE", "VIDEO" };
    static char path[512];
    uint32_t t0 = platform_ms();
    MemCard card;
    const char *dev = dev_of(slot);
    memory_card_read(dev, &card);
    cluster = card.cluster;
    entries = 0;
    read_error = 0;
    for (unsigned i = 0; i < sizeof ROOTS / sizeof ROOTS[0] && !stop_req; i++) {
        sceKernelWaitSema(lock, 1, NULL);
        snprintf(cur_dir, sizeof cur_dir, "%s", ROOTS[i]);
        sceKernelSignalSema(lock, 1);
        snprintf(path, sizeof path, "%s/%s", dev, ROOTS[i]);
        walk(path, path + 5, 0, 0);
    }
    if (!stop_req && !read_error) {
        memory_card_read(dev, &card);   // files may have changed meanwhile: "used" at the end
        sceKernelWaitSema(lock, 1, NULL);
        memstat_finish(&work, card.ok ? card.total - card.free : 0);
        done[slot] = work;
        have_done[slot] = 1;
        cur_dir[0] = '\0';
        sceKernelSignalSema(lock, 1);
        const MemTotals *d = &done[slot];
        log_msg("time: memory_count %u ms", (unsigned)(platform_ms() - t0));
        log_msg("mem: scan %s %u ms, %ld files, iso=%lld game=%lld dlc=%lld save=%lld music=%lld photo=%lld video=%lld "
                "sys=%lld other=%lld", dev, (unsigned)(platform_ms() - t0), d->files, d->bytes[MC_ISO],
                d->bytes[MC_GAME], d->bytes[MC_DLC], d->bytes[MC_SAVE], d->bytes[MC_MUSIC],
                d->bytes[MC_PHOTO], d->bytes[MC_VIDEO], d->bytes[MC_SYS], d->bytes[MC_OTHER]);
    } else {
        log_msg("mem: scan %s after %u ms, %ld files", read_error ? "broken by a read error" : "stopped",
                (unsigned)(platform_ms() - t0), work.files);
    }
    running = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

void memory_scan_start(const char *dev) {
    if (running) return;
    if (lock < 0) lock = sceKernelCreateSema("nemo_mem", 0, 1, 1, NULL);
    if (lock < 0) return;
    memset(&work, 0, sizeof work);
    slot = slot_of(dev);
    stop_req = 0;
    running = 1;
    SceUID th = sceKernelCreateThread("nemo_mem_scan", scan_thread, SCAN_PRIO, SCAN_STACK, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        running = 0;
        log_msg("mem: scan thread failed %08x", (unsigned)th);
    }
}

void memory_scan_stop(void) {
    if (running) stop_req = 1;
}

int memory_scan_state(const char *dev, MemTotals *out, char *cur, size_t cap) {
    if (cur && cap) cur[0] = '\0';
    if (lock < 0) return -1;
    int s = slot_of(dev);
    sceKernelWaitSema(lock, 1, NULL);
    // A stopping count still blocks a new one; a count of the other storage leaves this one's result.
    int st = running && slot == s ? 0 : have_done[s] ? 1 : -1;
    if (st == 0) {
        *out = work;
        if (cur && cap) snprintf(cur, cap, "%s", cur_dir);
    } else if (st == 1) {
        *out = done[s];
    }
    sceKernelSignalSema(lock, 1);
    return st;
}

int memory_scan_busy(void) { return running; }
