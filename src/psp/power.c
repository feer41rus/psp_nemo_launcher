#include "power.h"
#include "log.h"
#include "platform.h"
#include <kubridge.h>
#include <pspkernel.h>
#include <psppower.h>
#include <systemctrl.h>
#include <string.h>

static volatile int suspended, resumed, suspend_ready;
static volatile unsigned last_arg;
static int screen_off;
static int listening;
static int saved_cpu, saved_bus;      // clock before listening mode lowered it
static void *display_disable, *display_enable;   // sceDisplay_driver (kernel)

static int power_callback(int unknown, int arg, void *common) {
    (void)unknown;
    (void)common;
    last_arg = (unsigned)arg;
    if (arg & PSP_POWER_CB_SUSPENDING) {
        // Hold the suspend (up to 1 s) until the main thread saved the music position.
        suspend_ready = 0;
        suspended = 1;
        for (int i = 0; i < 100 && !suspend_ready; i++) sceKernelDelayThread(10000);
    }
    if (arg & PSP_POWER_CB_RESUME_COMPLETE) resumed = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int cb = sceKernelCreateCallback("nemo_power", power_callback, NULL);
    scePowerRegisterCallback(-1, cb);
    sceKernelSleepThreadCB();
    return 0;
}

static void *find_display(u32 new_nid, u32 old_nid) {
    void *f = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", new_nid);
    if (!f) f = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", old_nid);
    return f;
}

void power_init(void) {
    SceUID th = sceKernelCreateThread("nemo_power_cb", callback_thread, 0x11, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    // NIDs from ARK-5 SystemControl/src/nid_660_data.c (6.60 value first, then the old one).
    display_disable = find_display(0x33B620AF, 0x681EE6A7);
    display_enable = find_display(0x117C3E2C, 0x432D133F);
    log_msg("power: display disable=%p enable=%p", display_disable, display_enable);
}

unsigned power_last_arg(void) { return last_arg; }

int power_take_suspended(void) {
    if (!suspended) return 0;
    suspended = 0;
    return 1;
}

void power_suspend_done(void) { suspend_ready = 1; }

int power_take_resumed(void) {
    if (!resumed) return 0;
    resumed = 0;
    return 1;
}

static int kcall(void *f) {
    if (!f) return -1;
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    int rc = kuKernelCall(f, &args);
    return rc < 0 ? rc : (int)args.ret1;
}

// Listening clocks (measured on the device: lower values stutter).
static int eq_active;
static AudioFormat listen_fmt = AF_MP3;

static void listening_clock(AudioFormat fmt) {
    // 111 MHz stuttered on the author's PSP with software MP3 decoding; 222 keeps headroom.
    // The equalizer adds float filtering in the mixer: 266 then (1.2.1).
    listen_fmt = fmt;
    int mp3 = fmt == AF_MP3 && !eq_active;
    int cpu = mp3 ? 222 : 266, bus = mp3 ? 111 : 133;
    int rc = scePowerSetClockFrequency(cpu, cpu, bus);
    log_msg("power: clock %d/%d for %s rc=%d", cpu, bus, mp3 ? "mp3" : "other", rc);
}

void power_set_eq(int active) {
    if (eq_active == active) return;
    eq_active = active;
    if (listening) listening_clock(listen_fmt);
}

void power_set_listening(int on, AudioFormat fmt) {
    if (on) {
        if (!listening) {
            saved_cpu = scePowerGetCpuClockFrequency();
            saved_bus = scePowerGetBusClockFrequency();
        }
        listening_clock(fmt);
        if (!screen_off) {
            int rc = kcall(display_disable);
            screen_off = 1;
            log_msg("power: screen off rc=%d", rc);
        }
        listening = 1;
    } else {
        if (screen_off) {
            int rc = kcall(display_enable);
            screen_off = 0;
            log_msg("power: screen on rc=%d", rc);
        }
        if (listening && saved_cpu > 0) scePowerSetClockFrequency(saved_cpu, saved_cpu, saved_bus);
        listening = 0;
    }
}

int power_screen_is_off(void) { return screen_off; }

void power_set_screen(int on) {
    if (on == !screen_off) return;
    int rc = kcall(on ? display_enable : display_disable);
    screen_off = !on;
    log_msg("power: screen %s rc=%d (idle)", on ? "on" : "off", rc);
}

void power_sleep(void) {
    log_msg("power: sleep requested");
    if (screen_off) power_set_listening(0, AF_MP3);
    scePowerRequestSuspend();
}

void power_reboot(void) {
    log_msg("power: restart requested");
    scePowerRequestColdReset(0);
}

void power_shutdown(void) {
    log_msg("power: shutdown requested");
    scePowerRequestStandby();
}

void power_log_minute(AudioFormat fmt, unsigned underruns) {
    static const char *const NAMES[] = { "mp3", "flac", "ogg", "wav", "?" };
    Status st;
    platform_status(&st);   // sanitized battery (a firmware error code is not a percent)
    log_msg("power: cpu=%d bus=%d battery=%d%% charging=%d screen=%s fmt=%s underruns=%u",
            scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(), st.battery_pct,
            st.charging, screen_off ? "off" : "on", NAMES[fmt <= AF_UNKNOWN ? fmt : AF_UNKNOWN],
            underruns);
}
