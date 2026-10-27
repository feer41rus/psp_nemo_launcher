// Battery screens (1.2.2): details, the capacity test with its own game-like load, and test results.
// Readings come from battery_hw.c (read-only); the test log format and summaries from core/battery.c.
#include "ui.h"
#include "gfx.h"
#include "battery.h"
#include "batmodel.h"
#include "battery_hw.h"
#include "device.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "power.h"
#include "player.h"
#include "rtc.h"
#include "sysreg.h"
#include <SDL2/SDL.h>
#include <dirent.h>
#include <math.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BAT_DIR "ms0:/PSP/NEMO/battery"
#define BAT_KEEP 5
#define BAT_SAMPLE_MS 10000             // one log line every 10 s (a power-off loses at most 10 s)
#define BAT_GUARD BAT_DIR "/restore.txt"   // power settings to put back after the test
#define PS_DIR "/CONFIG/SYSTEM/POWER_SAVING"
#define BAT_MODEL BAT_DIR "/model.txt"    // learned drain coefficients (batmodel.h)
#define MUSIC_OFF_LOAD 15                 // screen off: only music decoding runs (% of the clock)
#define BAT_MAX_PTS 300
#define BAT_LOG_MAX (256 * 1024)
#define LOAD_BUF (2 * 1024 * 1024)      // memory load: copied back and forth (fits a PSP-1000)
#define READ_CHUNK (64 * 1024)
#define READ_RATE_KBS 256
#define PARTICLES_STD 300
#define PARTICLES_LIGHT 20

typedef enum { BC_NONE, BC_START, BC_ABORT, BC_DELETE } BatConfirm;

typedef struct { float x, y, vx, vy, ang, va, size; Uint8 r, g, b, a; } Particle;

typedef struct BatteryScreen {
    BatScreenKind kind;
    int sel;
    BatConfirm confirm;
    int confirm_yes;
    BatInfo info;
    uint32_t info_ms;
    // results
    char names[BAT_KEEP][24];
    BatSummary sums[BAT_KEEP];
    int count;
    BatSample pts[BAT_MAX_PTS];
    int n_pts, pts_for;
    // running test
    BatTestMode mode;
    char log_path[96];
    uint32_t start_ms, last_log_ms, last_tick_ms;
    int logged, saved_cpu, saved_bus, saved_bright, start_mode;
    Particle *parts;
    int n_parts;
    SDL_Texture *tex;
} BatteryScreen;

// ---------- load threads (standard mode) ----------

static volatile int load_quit;
static volatile uint32_t read_bytes;
static SceUID cpu_th = -1, read_th = -1;
static uint8_t *load_buf;
static char read_path[320];

// Arithmetic plus a 2 MB copy back and forth, never sleeping: runs whenever the UI thread waits for vsync
// (lower priority, so the test screen stays smooth). One thread: equal priorities do not time-slice.
static int cpu_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    volatile float acc = 1.0f;
    while (!load_quit) {
        for (int i = 0; i < 20000; i++) acc = acc * 1.000001f + sinf((float)i) * 0.5f;
        if (load_buf) {
            memcpy(load_buf + LOAD_BUF / 2, load_buf, LOAD_BUF / 2);
            memcpy(load_buf, load_buf + LOAD_BUF / 2, LOAD_BUF / 2);
        }
    }
    return 0;
}

// Reads a big file in 64 KB chunks at about 256 KB/s, round and round (like a game streaming its ISO).
static int read_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    static uint8_t chunk[READ_CHUNK];
    SceUID fd = sceIoOpen(read_path, PSP_O_RDONLY, 0);
    while (fd >= 0 && !load_quit) {
        int n = sceIoRead(fd, chunk, READ_CHUNK);
        if (n <= 0) { sceIoLseek32(fd, 0, PSP_SEEK_SET); continue; }
        read_bytes += (uint32_t)n;
        sceKernelDelayThread(1000000 / (READ_RATE_KBS * 1024 / READ_CHUNK));
    }
    if (fd >= 0) sceIoClose(fd);
    return 0;
}

static void consider(const char *path, long long *best) {
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > *best && st.st_size > 4 * READ_CHUNK) {
        *best = st.st_size;
        snprintf(read_path, sizeof read_path, "%s", path);
    }
}

// The biggest ISO/CSO in ms0:/ISO, else the biggest track of the library; "" if none.
static void pick_read_file(const App *a) {
    long long best = 0;
    read_path[0] = 0;
    DIR *d = opendir("ms0:/ISO");
    if (d) {
        struct dirent *e;
        char p[300];
        while ((e = readdir(d)) != NULL) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || (strcasecmp(dot, ".iso") != 0 && strcasecmp(dot, ".cso") != 0)) continue;
            snprintf(p, sizeof p, "ms0:/ISO/%s", e->d_name);
            consider(p, &best);
        }
        closedir(d);
    }
    if (best == 0)   // no ISO: the biggest track
        for (int i = 0; i < a->mlib.track_count; i++) consider(a->mlib.tracks[i].path, &best);
}

static void load_start(const App *a) {
    load_quit = 0;
    read_bytes = 0;
    load_buf = malloc(LOAD_BUF);
    if (load_buf) memset(load_buf, 0x5a, LOAD_BUF);
    cpu_th = sceKernelCreateThread("nemo_bat_cpu", cpu_thread, 0x50, 0x4000, 0, NULL);
    if (cpu_th >= 0) sceKernelStartThread(cpu_th, 0, NULL);
    pick_read_file(a);
    if (read_path[0]) {
        read_th = sceKernelCreateThread("nemo_bat_read", read_thread, 0x30, 0x4000, 0, NULL);
        if (read_th >= 0) sceKernelStartThread(read_th, 0, NULL);
    }
    log_msg("battery: load started, buffer %s, reading %s", load_buf ? "2 MB" : "none", read_path[0] ? read_path : "-");
}

static void load_stop(void) {
    load_quit = 1;
    SceUInt t = 2000000;
    if (cpu_th >= 0) { sceKernelWaitThreadEnd(cpu_th, &t); sceKernelDeleteThread(cpu_th); cpu_th = -1; }
    t = 2000000;
    if (read_th >= 0) { sceKernelWaitThreadEnd(read_th, &t); sceKernelDeleteThread(read_th); read_th = -1; }
    free(load_buf);
    load_buf = NULL;
}

// ---------- helpers ----------

static int lang_en(void) { return i18n_get() == LANG_EN; }

static StrId state_label(const BatInfo *b) {
    if (!b->exists) return S_BAT_NONE;
    if (b->charging) return b->pct >= 100 ? S_BAT_CHARGED : S_BAT_CHARGING;
    return S_BAT_ON_BATTERY;
}

static void refresh(BatteryScreen *s, uint32_t now, int full) {
    if (s->info_ms && now - s->info_ms < 1000) return;
    battery_hw_read(&s->info, full);
    s->info_ms = now ? now : 1;
}

static int append_file(const char *path, const char *text) {
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fputs(text, f);
    fclose(f);   // closed after every line: a power cut keeps what was written
    return 0;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(BAT_LOG_MAX + 1);
    size_t n = buf ? fread(buf, 1, BAT_LOG_MAX, f) : 0;
    fclose(f);
    if (buf) buf[n] = 0;
    return buf;
}

// Our log names in BAT_DIR, newest first.
static int list_logs(char names[][24], int max) {
    char all[64][24];
    int n = 0;
    DIR *d = opendir(BAT_DIR);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 64)
        if ((strlen(e->d_name) == 17 || strlen(e->d_name) == 19) && strcmp(e->d_name + strlen(e->d_name) - 4, ".csv") == 0)
            snprintf(all[n++], 24, "%s", e->d_name);
    closedir(d);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(all[j - 1], all[j]) < 0; j--) {
            char t[24];
            memcpy(t, all[j], 24);
            memcpy(all[j], all[j - 1], 24);
            memcpy(all[j - 1], t, 24);
        }
    int m = n < max ? n : max;
    for (int i = 0; i < m; i++) memcpy(names[i], all[i], 24);
    return m;
}

static void prune_logs(void) {
    char names[64][24];
    const char *ptr[64];
    int del[64], n = list_logs(names, 64);
    for (int i = 0; i < n; i++) ptr[i] = names[i];
    int k = batlog_prune(ptr, n, BAT_KEEP, del);
    for (int i = 0; i < k; i++) {
        char p[160];
        snprintf(p, sizeof p, BAT_DIR "/%s", names[del[i]]);
        remove(p);
        log_msg("battery: old log %s removed", p);
    }
}

static void load_results(BatteryScreen *s) {
    s->count = list_logs(s->names, BAT_KEEP);
    s->pts_for = -1;
    for (int i = 0; i < s->count; i++) {
        char p[160];
        snprintf(p, sizeof p, BAT_DIR "/%s", s->names[i]);
        char *t = read_file(p);
        int n;
        if (!t || batlog_parse(t, &s->sums[i], NULL, 0, &n) != 0) memset(&s->sums[i], 0, sizeof s->sums[i]);
        free(t);
    }
}

static void load_points(BatteryScreen *s) {
    if (s->pts_for == s->sel || s->sel >= s->count) return;
    char p[160];
    snprintf(p, sizeof p, BAT_DIR "/%s", s->names[s->sel]);
    char *t = read_file(p);
    BatSummary sum;
    s->n_pts = 0;
    if (t) batlog_parse(t, &sum, s->pts, BAT_MAX_PTS, &s->n_pts);
    free(t);
    s->pts_for = s->sel;
}

// ---------- test run ----------

static void particles_init(BatteryScreen *s) {
    s->n_parts = s->mode == BT_STANDARD ? PARTICLES_STD : PARTICLES_LIGHT;
    s->parts = calloc((size_t)s->n_parts, sizeof *s->parts);
    if (!s->parts) s->n_parts = 0;
    for (int i = 0; i < s->n_parts; i++) {
        Particle *p = &s->parts[i];
        float speed = s->mode == BT_STANDARD ? 3.0f : 0.4f;
        p->x = (float)(rand() % SCREEN_W);
        p->y = (float)(rand() % SCREEN_H);
        p->vx = ((rand() % 200) - 100) / 100.0f * speed;
        p->vy = ((rand() % 200) - 100) / 100.0f * speed;
        p->ang = (float)(rand() % 360);
        p->va = ((rand() % 200) - 100) / 20.0f * (s->mode == BT_STANDARD ? 1.0f : 0.2f);
        p->size = (float)(12 + rand() % (s->mode == BT_STANDARD ? 40 : 28));
        p->r = (Uint8)(60 + rand() % 196);
        p->g = (Uint8)(60 + rand() % 196);
        p->b = (Uint8)(120 + rand() % 136);
        p->a = (Uint8)(s->mode == BT_STANDARD ? 90 + rand() % 120 : 70);
    }
    // A soft round sprite: drawn by the GPU with rotation, colour and alpha.
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, 32, 32, 32, SDL_PIXELFORMAT_ABGR8888);
    if (sf) {
        Uint32 *px = sf->pixels;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                float dx = x - 15.5f, dy = y - 15.5f, d = sqrtf(dx * dx + dy * dy) / 16.0f;
                int a = d >= 1.0f ? 0 : (int)((1.0f - d) * 255);
                px[y * (sf->pitch / 4) + x] = ((Uint32)a << 24) | 0xffffff;
            }
        s->tex = SDL_CreateTextureFromSurface(gfx_renderer(), sf);
        SDL_FreeSurface(sf);
        if (s->tex) SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_ADD);
    }
}

static void test_log(App *a, BatteryScreen *s) {
    BatInfo b;
    battery_hw_read(&b, 1);
    uint32_t kb = read_bytes / 1024, free_b = 0, total_b = 0;
    static uint32_t prev_kb;
    platform_memory(&free_b, &total_b);
    uint32_t sec = (a->now_ms - s->start_ms) / 1000;
    BatSample smp = { (int)sec, b.pct, b.mv, b.temp, b.ma, perfstat_load(&a->perf), a->fps,
                      s->logged ? (int)((kb - prev_kb) / (BAT_SAMPLE_MS / 1000)) : 0, (int)(free_b / 1024) };
    prev_kb = kb;
    char line[96];
    batlog_line(line, sizeof line, &smp);
    if (append_file(s->log_path, line) != 0) log_msg("battery: log write failed %s", s->log_path);
    s->logged++;
}

// Auto sleep and backlight auto-off off for the test (system idle timer too); the old values go to a
// file first, so they come back at the end of the test or, after a power-off, at the next start.
static void guard_on(void) {
    BatGuard g = { -1, -1, platform_impose_get(IMPOSE_BRIGHTNESS) };
    sysreg_get_int(PS_DIR, "suspend_interval", &g.suspend_s);
    sysreg_get_int(PS_DIR, "backlight_off_interval", &g.backlight_s);
    char t[96];
    batguard_format(t, sizeof t, &g);
    FILE *f = fopen(BAT_GUARD, "w");
    if (f) { fputs(t, f); fclose(f); }
    if (!f) { log_msg("battery: cannot save power settings, leaving them as they are"); return; }
    if (g.suspend_s > 0) sysreg_set_int(PS_DIR, "suspend_interval", 0);
    if (g.backlight_s > 0) sysreg_set_int(PS_DIR, "backlight_off_interval", 0);
    int rc = scePowerIdleTimerDisable(0);
    log_msg("battery: power saving off (suspend=%d backlight=%d bright=%d, idle timer rc=%08x)", g.suspend_s,
            g.backlight_s, g.bright, (unsigned)rc);
}

// Puts back what guard_on saved; nothing to do without the file. 1 = restored.
static int guard_restore(App *a) {
    char *t = read_file(BAT_GUARD);
    if (!t) return 0;
    BatGuard g;
    int ok = batguard_parse(t, &g) == 0;
    free(t);
    if (ok) {
        if (g.suspend_s >= 0 && sysreg_set_int(PS_DIR, "suspend_interval", g.suspend_s) == 0) a->sleep_s = g.suspend_s;
        if (g.backlight_s >= 0 && sysreg_set_int(PS_DIR, "backlight_off_interval", g.backlight_s) == 0)
            a->backlight_s = g.backlight_s;
        if (g.bright >= 0) platform_impose_set(IMPOSE_BRIGHTNESS, g.bright);
    }
    scePowerIdleTimerEnable(0);
    remove(BAT_GUARD);
    log_msg("battery: power settings restored (%s: suspend=%d backlight=%d bright=%d)", ok ? "ok" : "file broken",
            ok ? g.suspend_s : -1, ok ? g.backlight_s : -1, ok ? g.bright : -1);
    return 1;
}

static void test_start(App *a, BatTestMode mode) {
    BatteryScreen *s = a->battery;
    mkdir("ms0:/PSP/NEMO", 0777);
    mkdir(BAT_DIR, 0777);
    ScePspDateTime now;   // seconds in the name: two tests in one minute keep two logs
    if (sceRtcGetCurrentClockLocalTime(&now) < 0) memset(&now, 0, sizeof now);
    char iso[32];
    snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d", now.year, now.month, now.day, now.hour, now.minute);
    snprintf(s->log_path, sizeof s->log_path, BAT_DIR "/%04d%02d%02d-%02d%02d%02d.csv", now.year, now.month,
             now.day, now.hour, now.minute, now.second);
    char head[160];
    batlog_header(head, sizeof head, mode, device_model_name(platform_model()), iso);
    remove(s->log_path);
    append_file(s->log_path, head);
    prune_logs();
    s->mode = mode;
    s->kind = BATS_RUNNING;
    s->confirm = BC_NONE;
    s->start_ms = s->last_log_ms = s->last_tick_ms = a->now_ms;
    s->logged = 0;
    s->saved_cpu = scePowerGetCpuClockFrequency();
    s->saved_bus = scePowerGetBusClockFrequency();
    s->saved_bright = platform_impose_get(IMPOSE_BRIGHTNESS);
    guard_on();
    if (player_active() && !player_paused()) player_pause(1);   // no sound in the test (author)
    if (device_has_wifi(platform_model()) && net_started()) net_shutdown();   // no Wi-Fi either
    if (mode == BT_STANDARD) {
        scePowerSetClockFrequency(222, 222, 111);   // at 333 MHz the PSP switched itself off after ~35 min (author)
        platform_impose_set(IMPOSE_BRIGHTNESS, 3);
        load_start(a);
    }
    particles_init(s);
    log_msg("battery: test %s started, log %s", mode == BT_STANDARD ? "standard" : "light", s->log_path);
    test_log(a, s);   // second 0
}

static void test_stop(App *a, int aborted) {
    BatteryScreen *s = a->battery;
    load_stop();
    char foot[32];
    batlog_footer(foot, sizeof foot, aborted);
    append_file(s->log_path, foot);
    if (s->saved_cpu > 0) scePowerSetClockFrequency(s->saved_cpu, s->saved_cpu, s->saved_bus);
    if (s->saved_bright >= 0) platform_impose_set(IMPOSE_BRIGHTNESS, s->saved_bright);
    guard_restore(a);
    free(s->parts);
    s->parts = NULL;
    s->n_parts = 0;
    if (s->tex) SDL_DestroyTexture(s->tex);
    s->tex = NULL;
    log_msg("battery: test stopped (aborted=%d) after %d records", aborted, s->logged);
    s->kind = BATS_TEST;
    s->sel = 2;
}

// ---------- Nemo's estimate (batmodel.h) ----------

static BatModel model;
static BatWindow model_win;
static int model_ready;

static void model_load(void) {
    batmodel_default(&model);
    batwin_reset(&model_win);
    char *t = read_file(BAT_MODEL);
    if (t && batmodel_parse(t, &model) != 0) batmodel_default(&model);
    free(t);
    model_ready = 1;
}

static void model_save(void) {
    char t[128];
    batmodel_format(t, sizeof t, &model);
    mkdir("ms0:/PSP/NEMO", 0777);
    mkdir(BAT_DIR, 0777);
    FILE *f = fopen(BAT_MODEL, "w");
    if (f) { fputs(t, f); fclose(f); }
}

static int bright_level(void) {
    if (power_screen_is_off()) return 0;
    int b = platform_impose_get(IMPOSE_BRIGHTNESS);   // 0..3
    return b >= 0 && b <= 3 ? b + 1 : 4;
}

// CPU share now: the standard test runs the load threads flat out; screen off is music only.
static float cpu_now(const App *a) {
    int mhz = scePowerGetCpuClockFrequency();
    if (battery_running(a) && a->battery->mode == BT_STANDARD) return batmodel_cpu(222, 100);   // load threads flat out
    if (power_screen_is_off()) return batmodel_cpu(mhz, MUSIC_OFF_LOAD);
    return batmodel_cpu(mhz, perfstat_load(&a->perf));
}

// Once a second, on battery: one more measurement for the model; saved when a sample is learned.
void battery_learn_tick(App *a) {
    static uint32_t last;
    if (!model_ready || a->now_ms - last < 1000) return;
    last = a->now_ms;
    BatInfo b;
    battery_hw_read(&b, 0);
    if (!b.exists) return;
    float cpu = cpu_now(a);
    int level = bright_level();
    if (batwin_add(&model_win, &model, a->now_ms, b.pct, b.charging, cpu, level)) {
        model_save();
        log_msg("battery: learned %d.%03d %%/min (cpu %d%%, level %d), model base=%d cpu=%d bright=%d (1/1000), n=%d",
                (int)(model_win.last_rate * 1000) / 1000, (int)(model_win.last_rate * 1000) % 1000,
                (int)(cpu * 100), level, (int)(model.base * 1000), (int)(model.k_cpu * 1000),
                (int)(model.k_bright * 1000), model.samples);
    }
}

// ---------- public ----------

void battery_open(App *a, BatScreenKind k) {
    if (!a->battery) a->battery = calloc(1, sizeof *a->battery);
    if (!a->battery) return;
    BatteryScreen *s = a->battery;
    s->kind = k;
    s->sel = 0;
    s->confirm = BC_NONE;
    s->info_ms = 0;
    if (k == BATS_RESULTS) load_results(s);
}

int battery_running(const App *a) { return a->battery && a->battery->kind == BATS_RUNNING; }

static void close_screen(App *a) {
    free(a->battery);
    a->battery = NULL;
}

void battery_action(App *a, Action act) {
    BatteryScreen *s = a->battery;
    if (!s) return;
    if (s->confirm != BC_NONE) {                  // yes / no question
        if (act == ACT_LEFT || act == ACT_RIGHT || act == ACT_UP || act == ACT_DOWN) s->confirm_yes = !s->confirm_yes;
        if (act == ACT_BACK) s->confirm = BC_NONE;
        if (act != ACT_CONFIRM) return;
        BatConfirm c = s->confirm;
        s->confirm = BC_NONE;
        if (!s->confirm_yes) return;
        if (c == BC_START) test_start(a, s->start_mode ? BT_LIGHT : BT_STANDARD);
        else if (c == BC_ABORT) test_stop(a, 1);
        else if (c == BC_DELETE && s->sel < s->count) {
            char p[160];
            snprintf(p, sizeof p, BAT_DIR "/%s", s->names[s->sel]);
            remove(p);
            load_results(s);
            if (s->sel >= s->count) s->sel = s->count ? s->count - 1 : 0;
        }
        return;
    }
    switch (s->kind) {
    case BATS_RUNNING:                            // owns every button; only ○ (stop) does something
        if (act == ACT_BACK) { s->confirm = BC_ABORT; s->confirm_yes = 0; }
        return;
    case BATS_DETAILS:
        if (act == ACT_BACK || act == ACT_HOME) close_screen(a);
        return;
    case BATS_TEST:
        if (act == ACT_BACK || act == ACT_HOME) { close_screen(a); return; }
        if (act == ACT_UP && s->sel > 0) s->sel--;
        if (act == ACT_DOWN && s->sel < 2) s->sel++;
        if (act != ACT_CONFIRM) return;
        if (s->sel == 2) { battery_open(a, BATS_RESULTS); return; }
        battery_hw_read(&s->info, 0);
        s->start_mode = s->sel;
        if (s->info.charging || s->info.pct < 95) { s->confirm = BC_START; s->confirm_yes = 0; }
        else test_start(a, s->sel ? BT_LIGHT : BT_STANDARD);
        return;
    case BATS_RESULTS:
        if (act == ACT_HOME) { close_screen(a); return; }
        if (act == ACT_BACK) { battery_open(a, BATS_TEST); a->battery->sel = 2; return; }
        if (act == ACT_UP && s->sel > 0) s->sel--;
        if (act == ACT_DOWN && s->sel + 1 < s->count) s->sel++;
        if (act == ACT_MENU && s->count) { s->confirm = BC_DELETE; s->confirm_yes = 0; }
        return;
    }
}

void battery_tick(App *a) {
    BatteryScreen *s = a->battery;
    if (!s) return;
    if (s->kind != BATS_RUNNING) {
        refresh(s, a->now_ms, s->kind == BATS_DETAILS);
        return;
    }
    if (a->now_ms - s->last_tick_ms >= 1000) {   // keep the PSP from dimming or sleeping during the test
        scePowerTick(PSP_POWER_TICK_ALL);
        s->last_tick_ms = a->now_ms;
        s->info_ms = 0;
        refresh(s, a->now_ms, 0);
    }
    if (a->now_ms - s->last_log_ms >= BAT_SAMPLE_MS) {
        s->last_log_ms += BAT_SAMPLE_MS;
        test_log(a, s);
    }
}

// Unfinished log (the PSP switched itself off during a test): close it and tell the result. Power
// settings the test switched off come back first. Nemo's estimate model is loaded here too.
void battery_startup_check(App *a) {
    guard_restore(a);
    model_load();
    char names[BAT_KEEP][24];
    int n = list_logs(names, BAT_KEEP);
    for (int i = 0; i < n; i++) {
        char p[160];
        snprintf(p, sizeof p, BAT_DIR "/%s", names[i]);
        char *t = read_file(p);
        if (!t) continue;
        int open = strstr(t, "# end") == NULL && strncmp(t, "# nemo-battery", 14) == 0;
        BatSummary sum;
        int np;
        if (open && batlog_parse(t, &sum, NULL, 0, &np) == 0) {
            char dur[32], msg[128];
            int cut = batlog_cut_short(&sum);   // charge left: not the battery, something switched it off
            battery_fmt_minutes(dur, sizeof dur, sum.minutes, lang_en());
            if (cut) snprintf(msg, sizeof msg, tr(S_BAT_CUT_FMT), sum.pct_end, dur);
            else snprintf(msg, sizeof msg, tr(S_BAT_DONE_FMT), dur);
            ui_toast(a, msg);
            log_msg("battery: test %s %s after %d min at %d%%", names[i],
                    cut ? "cut short by a power-off" : "finished by self power-off", sum.minutes, sum.pct_end);
            char foot[32];
            batlog_footer(foot, sizeof foot, cut);
            append_file(p, foot);
        }
        free(t);
    }
}

// ---------- drawing ----------

static void draw_warning(int y) {
    if (!battery_unreliable(platform_model())) return;
    gfx_text_fit(FONT_TINY, 14, y, SCREEN_W - 28, 0xe0b45aff, tr(S_BAT_UNRELIABLE));
}

static void kv(int x, int y, int w, StrId label, const char *value) {
    gfx_text(FONT_SMALL, x, y, COL_MUTED, tr(label));
    gfx_text(FONT_SMALL, x + w - gfx_text_width(FONT_SMALL, value), y, COL_TEXT, value);
}

static void draw_details(const App *a, const BatteryScreen *s) {
    const BatInfo *b = &s->info;
    int en = lang_en();
    gfx_text(FONT_BOLD, 16, CONTENT_Y + 6, COL_TEXT, tr(S_BAT_DETAILS));
    char v[64];
    if (b->pct >= 0) snprintf(v, sizeof v, "%d", b->pct);
    else snprintf(v, sizeof v, "—");
    gfx_text(FONT_TITLE, 16, CONTENT_Y + 34, COL_TEXT, v);
    gfx_text(FONT_SMALL, 16 + gfx_text_width(FONT_TITLE, v) + 6, CONTENT_Y + 44, COL_MUTED, tr(state_label(b)));
    int bw = 200, pct = b->pct > 0 ? b->pct : 0;
    gfx_round_rect(16, CONTENT_Y + 70, bw, 10, 5, 0x2a3442ff);
    if (pct) gfx_round_rect(16, CONTENT_Y + 70, bw * pct / 100, 10, 5, pct < 20 ? 0xd9534fff : 0x3fae84ff);
    int x = 236, w = SCREEN_W - x - 14, y = CONTENT_Y + 30, lh = 15;
    if (model_ready && b->exists && !b->charging) {   // Nemo's estimate: now, and in a game at this brightness
        char t[32];
        int ey = CONTENT_Y + 94, level = bright_level();
        battery_fmt_minutes(t, sizeof t, batmodel_minutes(&model, b->pct, cpu_now(a), level), en);
        snprintf(v, sizeof v, "~%s", t);   // no "≈" in the font
        kv(16, ey, 200, S_BAT_EST_NOW, v);
        battery_fmt_minutes(t, sizeof t, batmodel_minutes(&model, b->pct, 1.0f, level), en);
        snprintf(v, sizeof v, "~%s", t);   // no "≈" in the font
        kv(16, ey + lh, 200, S_BAT_EST_GAME, v);
        if (model.samples < BATMODEL_SURE) gfx_text(FONT_TINY, 16, ey + 2 * lh + 2, COL_MUTED, tr(S_BAT_EST_LEARNING));
    }
    battery_fmt_volts(v, sizeof v, b->mv, en);
    kv(x, y, w, S_BAT_VOLT, v), y += lh;
    if (b->ma != -1) snprintf(v, sizeof v, en ? "%d mA" : "%d мА", b->ma); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_CURRENT, v), y += lh;
    if (b->temp >= 0) snprintf(v, sizeof v, "%d °C", b->temp); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_TEMP, v), y += lh;
    battery_fmt_minutes(v, sizeof v, b->life_min, en);
    kv(x, y, w, S_BAT_LEFT, v), y += lh;
    if (b->rcap >= 0 && b->fcap > 0) snprintf(v, sizeof v, tr(S_BAT_CAP_FMT), b->rcap, b->fcap); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_CAP, v), y += lh;
    int design = battery_design_mah(platform_model()), wear = battery_wear_pct(b->fcap, design);
    if (wear >= 0) snprintf(v, sizeof v, tr(S_BAT_WEAR_FMT), wear, design); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_WEAR, v), y += lh;
    if (b->cycles >= 0) snprintf(v, sizeof v, "%d", b->cycles); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_CYCLES, v), y += lh;
    if (b->have_serial) snprintf(v, sizeof v, "0x%08X", b->serial); else snprintf(v, sizeof v, "—");
    kv(x, y, w, S_BAT_SERIAL, v), y += lh;
    if (b->total_elec > 0) { snprintf(v, sizeof v, en ? "%d mAh" : "%d мА·ч", b->total_elec); kv(x, y, w, S_BAT_TOTAL, v), y += lh; }
    if (b->charge_time > 0) { battery_fmt_minutes(v, sizeof v, b->charge_time, en); kv(x, y, w, S_BAT_CHARGE_TIME, v); }
    draw_warning(SCREEN_H - HINT_BAR_H - 26);
}

static void draw_row(int y, int sel, StrId label, StrId sub) {
    int x = 12, w = SCREEN_W - 24;
    gfx_round_rect(x, y, w, 22, 6, sel ? 0x24324aff : COL_PANEL);
    if (sel) gfx_round_rect_outline(x, y, w, 22, 6, COL_ACCENT);
    gfx_text(FONT_BODY, x + 10, y + 4, COL_TEXT, tr(label));
    if (sub) gfx_text(FONT_SMALL, x + w - 10 - gfx_text_width(FONT_SMALL, tr(sub)), y + 6, COL_MUTED, tr(sub));
}

static void draw_test_menu(const BatteryScreen *s) {
    gfx_text(FONT_BOLD, 16, CONTENT_Y + 6, COL_TEXT, tr(S_BAT_TEST));
    gfx_text_fit(FONT_SMALL, 16, CONTENT_Y + 28, SCREEN_W - 32, COL_MUTED, tr(S_BAT_HINT_TEST));
    draw_row(CONTENT_Y + 48, s->sel == 0, S_BAT_STANDARD, S_BAT_STANDARD_SUB);
    draw_row(CONTENT_Y + 74, s->sel == 1, S_BAT_LIGHT, S_BAT_LIGHT_SUB);
    draw_row(CONTENT_Y + 100, s->sel == 2, S_BAT_RESULTS, 0);
    gfx_text_fit(FONT_TINY, 14, CONTENT_Y + 132, SCREEN_W - 28, COL_MUTED, tr(S_BAT_TEST_NOTE));
    draw_warning(SCREEN_H - HINT_BAR_H - 26);
}

static void draw_running(const App *a, const BatteryScreen *s) {
    SDL_Renderer *r = gfx_renderer();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x05070bff);
    if (s->tex) {                                  // the load picture: particles drawn by the GPU
        for (int i = 0; i < s->n_parts; i++) {
            Particle *p = &s->parts[i];
            p->x += p->vx;
            p->y += p->vy;
            p->ang += p->va;
            if (p->x < -40) p->x += SCREEN_W + 80;
            if (p->x > SCREEN_W + 40) p->x -= SCREEN_W + 80;
            if (p->y < -40) p->y += SCREEN_H + 80;
            if (p->y > SCREEN_H + 40) p->y -= SCREEN_H + 80;
            SDL_SetTextureColorMod(s->tex, p->r, p->g, p->b);
            SDL_SetTextureAlphaMod(s->tex, p->a);
            SDL_Rect d = { (int)(p->x - p->size / 2), (int)(p->y - p->size / 2), (int)p->size, (int)p->size };
            SDL_RenderCopyEx(r, s->tex, NULL, &d, p->ang, NULL, SDL_FLIP_NONE);
        }
    }
    int en = lang_en();
    gfx_round_rect(10, 10, 220, 112, 8, 0x000000a0);
    char v[64], t[48];
    gfx_text(FONT_BOLD, 18, 16, COL_TEXT, tr(s->mode == BT_STANDARD ? S_BAT_STANDARD : S_BAT_LIGHT));
    uint32_t mins = (a->now_ms - s->start_ms) / 60000;
    snprintf(v, sizeof v, "%u:%02u", (unsigned)(mins / 60), (unsigned)(mins % 60));
    gfx_text(FONT_TITLE, 18, 34, COL_TEXT, v);
    gfx_text(FONT_SMALL, 20 + gfx_text_width(FONT_TITLE, v) + 6, 44, COL_MUTED, tr(S_BAT_ELAPSED));
    const BatInfo *b = &s->info;
    battery_fmt_volts(t, sizeof t, b->mv, en);
    snprintf(v, sizeof v, "%d %% · %s · %d °C", b->pct, t, b->temp);
    gfx_text(FONT_SMALL, 18, 66, COL_TEXT, v);
    snprintf(v, sizeof v, "CPU %d %s · %d%% · FPS %d", scePowerGetCpuClockFrequency(), en ? "MHz" : "МГц",
             perfstat_load(&a->perf), a->fps);
    gfx_text(FONT_SMALL, 18, 82, COL_TEXT, v);
    if (s->mode == BT_STANDARD && read_path[0]) {
        static uint32_t prev_b, prev_ms, kbs;
        if (a->now_ms - prev_ms >= 1000) {
            kbs = (read_bytes - prev_b) / 1024 * 1000 / (a->now_ms - prev_ms ? a->now_ms - prev_ms : 1);
            prev_b = read_bytes;
            prev_ms = a->now_ms;
        }
        snprintf(v, sizeof v, "%s %u %s", tr(S_BAT_READ), (unsigned)kbs, en ? "KB/s" : "КБ/с");
    } else {
        snprintf(v, sizeof v, "%s", s->mode == BT_STANDARD ? tr(S_BAT_NOREAD) : "");
    }
    gfx_text(FONT_SMALL, 18, 98, COL_MUTED, v);
}

static void draw_results(BatteryScreen *s) {
    gfx_text(FONT_BOLD, 16, CONTENT_Y + 6, COL_TEXT, tr(S_BAT_RESULTS));
    if (!s->count) {
        gfx_text(FONT_BODY, 16, CONTENT_Y + 40, COL_MUTED, tr(S_BAT_NO_RESULTS));
        return;
    }
    int en = lang_en();
    for (int i = 0; i < s->count; i++) {           // list on the left
        const BatSummary *m = &s->sums[i];
        int y = CONTENT_Y + 28 + i * 34;
        gfx_round_rect(12, y, 128, 30, 6, i == s->sel ? 0x24324aff : COL_PANEL);
        if (i == s->sel) gfx_round_rect_outline(12, y, 128, 30, 6, COL_ACCENT);
        char l1[48], l2[48], d[24];
        const char *n = s->names[i];   // YYYYMMDD-HHMM.csv
        snprintf(l1, sizeof l1, "%.2s.%.2s %.2s:%.2s · %s", n + 6, n + 4, n + 9, n + 11,
                 m->mode == BT_LIGHT ? (en ? "Everyday" : "Обычн.") : (en ? "Std." : "Станд."));
        battery_fmt_minutes(d, sizeof d, m->minutes, en);
        if (m->aborted) snprintf(l2, sizeof l2, "%s · %s", tr(S_BAT_ABORTED), d);
        else snprintf(l2, sizeof l2, "%s", d);
        gfx_text_fit(FONT_SMALL, 18, y + 3, 118, COL_TEXT, l1);
        gfx_text_fit(FONT_SMALL, 18, y + 16, 118, COL_MUTED, l2);
    }
    load_points(s);
    int gx = 150, gy = CONTENT_Y + 28, gw = SCREEN_W - gx - 12, gh = SCREEN_H - HINT_BAR_H - gy - 6;
    gfx_round_rect(gx, gy, gw, gh, 8, 0x141b26ff);
    const BatSummary *m = &s->sums[s->sel];
    char v1[16], v2[16], head[120];
    battery_fmt_volts(v1, sizeof v1, m->mv_start, en);
    battery_fmt_volts(v2, sizeof v2, m->mv_end, en);
    snprintf(head, sizeof head, tr(S_BAT_SUMMARY_FMT), m->pct_start, m->pct_end, v1, v2, m->temp_max);
    gfx_text_fit(FONT_SMALL, gx + 8, gy + 6, gw - 16, COL_TEXT, head);
    int px = gx + 26, py = gy + 24, pw = gw - 36, ph = gh - 44;
    gfx_rect(px, py, 1, ph, 0x3a4658ff);
    gfx_rect(px, py + ph, pw, 1, 0x3a4658ff);
    gfx_text(FONT_TINY, gx + 6, py - 4, COL_MUTED, "100");
    gfx_text(FONT_TINY, gx + 14, py + ph - 6, COL_MUTED, "0");
    char d[24];
    battery_fmt_minutes(d, sizeof d, m->minutes, en);
    gfx_text(FONT_TINY, px + pw - gfx_text_width(FONT_TINY, d), py + ph + 3, COL_MUTED, d);
    if (s->n_pts < 2) return;
    int span = s->pts[s->n_pts - 1].sec > 0 ? s->pts[s->n_pts - 1].sec : 1;
    SDL_Renderer *r = gfx_renderer();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (int pass = 0; pass < 2; pass++) {         // 0: charge %, 1: voltage 3.3..4.3 V (dashed)
        SDL_SetRenderDrawColor(r, pass ? 0x6f : 0x3f, pass ? 0xb0 : 0xae, pass ? 0xff : 0x84, 0xff);
        for (int i = 1; i < s->n_pts; i++) {
            if (pass && i % 2) continue;
            const BatSample *p0 = &s->pts[i - 1], *p1 = &s->pts[i];
            int x0 = px + p0->sec * pw / span, x1 = px + p1->sec * pw / span;
            int y0, y1;
            if (!pass) { y0 = py + ph - p0->pct * ph / 100; y1 = py + ph - p1->pct * ph / 100; }
            else {
                int v0 = p0->mv < 3300 ? 3300 : p0->mv > 4300 ? 4300 : p0->mv;
                int w1 = p1->mv < 3300 ? 3300 : p1->mv > 4300 ? 4300 : p1->mv;
                y0 = py + ph - (v0 - 3300) * ph / 1000;
                y1 = py + ph - (w1 - 3300) * ph / 1000;
            }
            SDL_RenderDrawLine(r, x0, y0, x1, y1);
            if (!pass) SDL_RenderDrawLine(r, x0, y0 + 1, x1, y1 + 1);
        }
    }
}

static void draw_confirm(const BatteryScreen *s) {
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000a0);
    int w = 330, h = 104, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    StrId q = s->confirm == BC_START ? S_BAT_START_Q : s->confirm == BC_ABORT ? S_BAT_ABORT_Q : S_BAT_DEL_Q;
    gfx_text_fit(FONT_SMALL, x + 16, y + 16, w - 32, COL_TEXT, tr(q));
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int sel = (i == 0) == (s->confirm_yes != 0);
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, sel ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
        const char *lt = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, lt)) / 2, by + 4, COL_TEXT, lt);
    }
}

void battery_draw(const App *a) {
    BatteryScreen *s = a->battery;
    if (!s) return;
    if (s->kind != BATS_RUNNING) gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);
    switch (s->kind) {
    case BATS_DETAILS: draw_details(a, s); break;
    case BATS_TEST: draw_test_menu(s); break;
    case BATS_RUNNING: draw_running(a, s); break;
    case BATS_RESULTS: draw_results(s); break;
    }
    if (s->confirm != BC_NONE) draw_confirm(s);
}

void battery_draw_hints(const App *a) {
    const BatteryScreen *s = a->battery;
    if (!s) return;
    if (s->confirm != BC_NONE) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
        return;
    }
    switch (s->kind) {
    case BATS_RUNNING: {
        static const Hint h[] = { { ACT_BACK, S_HINT_ABORT } };
        ui_hints_draw(a, h, 1);
        break;
    }
    case BATS_TEST: {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_START }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
        break;
    }
    case BATS_RESULTS: {
        static const Hint h[] = { { ACT_UP, S_HINT_ROW }, { ACT_MENU, S_HINT_DELETE }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 3);
        break;
    }
    default: {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
        break;
    }
    }
}
