// Detailed log for beta testing (1.3.3, Settings › System › Debug, off by default): the console's
// "passport" at the start (or when it is turned on) and a snapshot of the state once a minute, so a
// tester's log tells everything without questions. The ordinary log stays as it was.
#include "ui.h"
#include "battery_hw.h"
#include "log.h"
#include "bt_hw.h"
#include "batcells.h"
#include "memory_hw.h"
#include "platform.h"
#include "player.h"
#include "safefile.h"
#include "usb.h"
#include "version.h"
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemctrl.h>

// A text file into the log, line by line (at most max_lines).
static void log_file_lines(const char *tag, const char *path, int max_lines) {
    int missing = 0;
    char *t = safe_read_text(path, 64 * 1024, &missing);
    if (!t) { log_msg("diag: %s: %s", tag, missing ? "none" : "unreadable"); return; }
    int n = 0;
    for (char *line = t; line && *line && n < max_lines; n++) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = '\0';
        log_msg("diag: %s: %s", tag, line);
        line = next;
    }
    free(t);
}

static void log_battery(const char *tag) {
    BatInfo b;
    battery_hw_read(&b, 1);
    log_msg("diag: %s battery: exists %d charging %d %d%% %d mV %d C %d mA life %d min, capacity %d/%d mAh, cycles %d", tag,
            b.exists, b.charging, b.pct, b.mv, b.temp, b.ma, b.life_min, b.rcap, b.fcap, b.cycles);
}

static void log_storage(const char *dev) {
    MemCard c;
    if (memory_card_read(dev, &c) == 0)
        log_msg("diag: %s: %lld MB free of %lld MB, cluster %u", dev, c.free >> 20, c.total >> 20, (unsigned)c.cluster);
    else log_msg("diag: %s: unreadable", dev);
}

void diag_passport(App *a) {
    log_msg("diag: ===== detailed log: passport =====");
    log_msg("diag: Nemo %s built %s %s", NEMO_VERSION, __DATE__, __TIME__);
    log_msg("diag: model %d, firmware %s, ARK %08X.%08X, ark path %s", platform_model(), platform_fw(),
            (unsigned)sctrlHENGetVersion(), (unsigned)sctrlHENGetMinorVersion(), platform_arkpath());
    log_msg("diag: clocks cpu %d MHz bus %d MHz", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
    platform_log_memory("diag");
    log_battery("start");
    log_storage("ms0:");
    if (platform_model() == 4) log_storage("ef0:");
    log_msg("diag: language %d, confirm %s, nickname \"%s\", volume %d, backlight %d s, sleep %d s", (int)i18n_get(),
            a->confirm_cross ? "cross" : "circle", a->nickname, a->volume, a->backlight_s, a->sleep_s);
    log_msg("diag: games %d, tracks %d, umd video %d", a->games.count, a->mlib.track_count, a->umd_video);
    long lib = platform_file_size(NEMO_DATA_PATH);
    log_msg("diag: library.json %ld bytes", lib);
    {   // Nemo's settings, as saved
        const NemoSettings *s = &a->lib->settings;
        log_msg("diag: nemo settings: log_keep %d verbose %d dbg %d/%d/%d show photo %d music %d", s->log_keep, s->verbose_log,
                s->dbg_ram, s->dbg_cpu, s->dbg_fps, s->show_photo, s->show_music);
    }
    char ark[200];
    snprintf(ark, sizeof ark, "%sSETTINGS.TXT", platform_arkpath());
    log_file_lines("ark settings", ark, 80);
    snprintf(ark, sizeof ark, "%sPLUGINS.TXT", platform_arkpath());
    log_file_lines("ark plugins", ark, 60);
    log_file_lines("seplugins", "ms0:/SEPLUGINS/PLUGINS.TXT", 60);
    log_file_lines("game plugins", "ms0:/SEPLUGINS/game.txt", 40);
    log_file_lines("vsh plugins", "ms0:/SEPLUGINS/vsh.txt", 40);
    if (platform_model() == 4) bt_refresh(&a->bt, 1);   // 1.3.12: where the PSP Go keeps Bluetooth
    log_msg("diag: ===== end of passport =====");
}

// 1.3.12: the battery icon's state in the detailed log whenever it changes (charger in/out, the next cell).
static void battery_watch(void) {
    static int last_charging = -1, last_cell = -1, last_red = -1;
    Status st;
    platform_status(&st);
    BatCells c;
    batcells(st.battery_pct, st.charging, 1, &c);
    int cell = st.charging ? c.blink : c.solid, red = c.solid_col == BC_RED || (c.blink >= 0 && c.blink_col == BC_RED);
    if (st.charging == last_charging && cell == last_cell && red == last_red) return;
    static const char *const COL[] = { "white", "grey", "green", "red" };
    log_msg("battery: icon %d %%, %s, %s %d of 4, %s", st.battery_pct, st.charging ? "charging" : "on battery",
            st.charging ? "blinking cell" : "filled cells", st.charging ? cell + 1 : cell,
            st.charging ? (c.blink >= 0 ? COL[c.blink_col] : st.battery_pct >= 100 ? "full, steady green" : "-") : COL[c.solid_col]);
    last_charging = st.charging;
    last_cell = cell;
    last_red = red;
}

void diag_tick(App *a) {
    static uint32_t last;
    if (log_verbose) battery_watch();
    if (!log_verbose || a->now_ms - last < 60000) return;
    last = a->now_ms;
    log_msg("diag: minute: screen %s, cpu %d MHz, music %s (track %d of %d, %u ms), usb %d", perflog_screen(),
            scePowerGetCpuClockFrequency(), player_active() ? (player_paused() ? "paused" : "playing") : "off",
            a->queue.pos + 1, a->queue.count, (unsigned)player_position_ms(), usb_active());
    platform_log_memory("minute");
    log_battery("minute");
    if (platform_model() == 4) bt_log_state("minute", &a->bt);
}
