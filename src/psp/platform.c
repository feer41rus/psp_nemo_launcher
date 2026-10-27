#include "platform.h"
#include <pspiofilemgr.h>
#include "sfo.h"
#include "pbp.h"
#include "safefile.h"
#include <psppower.h>
#include <psprtc.h>
#include <pspwlan.h>
#include <pspkernel.h>
#include <systemctrl.h>
#include <systemctrl_ark.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include "arksettings.h"
#include <kubridge.h>
#include <pspsysmem.h>
#include "device.h"
#include "log.h"

#define IMPOSE_MAIN_VOLUME 0x1

static ARKConfig ark_config;
static void *impose_get, *impose_set;   // sceImposeGetParam / sceImposeSetParam (driver)
static Status cached;
static uint32_t cached_at;
static int have_status;

void platform_init(void) {
    memset(&ark_config, 0, sizeof ark_config);
    sctrlArkGetConfig(&ark_config);
    if (!ark_config.arkpath[0]) strcpy(ark_config.arkpath, DEFAULT_ARK_PATH);
    // Same functions arkMenu uses (src/system_mgr.cpp).
    impose_get = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0xDC3BECFF);
    impose_set = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0x3C318569);
}

int platform_impose_get(int param) {
    if (!impose_get) return -1;
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    args.arg1 = (uint32_t)param;
    kuKernelCall(impose_get, &args);
    return (int)args.ret1;
}

void platform_impose_set(int param, int value) {
    if (!impose_set) return;
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    args.arg1 = (uint32_t)param;
    args.arg2 = (uint32_t)value;
    kuKernelCall(impose_set, &args);
}

int platform_volume_get(void) { return platform_impose_get(IMPOSE_MAIN_VOLUME); }
void platform_volume_set(int volume) { platform_impose_set(IMPOSE_MAIN_VOLUME, volume); }

const char *platform_arkpath(void) { return ark_config.arkpath; }

uint32_t platform_ms(void) { return (uint32_t)(sceKernelGetSystemTimeWide() / 1000); }

void platform_status(Status *s) {
    uint32_t now = platform_ms();
    if (!have_status || now - cached_at >= 1000) {
        cached.battery_pct = scePowerIsBatteryExist()
            ? battery_sanitize(scePowerGetBatteryLifePercent(), have_status ? cached.battery_pct : -1) : -1;
        cached.charging = scePowerIsBatteryCharging() == 1;
        cached.wifi_on = sceWlanGetSwitchState() == 1;
        ScePspDateTime t;
        if (sceRtcGetCurrentClockLocalTime(&t) >= 0) {
            cached.hour = t.hour;
            cached.minute = t.minute;
        }
        cached_at = now;
        have_status = 1;
    }
    *s = cached;
}

int platform_set_launcher_mode(int on) {
    char path[160];
    snprintf(path, sizeof path, "%s%s", ark_config.arkpath, ARK_SETTINGS);
    return arksettings_update_file(path, "flash1:/" ARK_SETTINGS, on);   // ARK falls back to flash1
}

int platform_model(void) {
    static int model = -2;
    if (model == -2) model = kuKernelGetModel();
    return model;
}

const char *platform_fw(void) {
    static char fw[16];
    if (!fw[0]) device_fw_string((uint32_t)sceKernelDevkitVersion(), fw, sizeof fw);
    return fw;
}

void platform_log_memory(const char *tag) {
    log_msg("mem[%s]: free %u KB, max block %u KB", tag, (unsigned)(sceKernelTotalFreeMemSize() / 1024),
            (unsigned)(sceKernelMaxFreeMemSize() / 1024));
}

char *platform_ark_read_text(void) {
    char path[160];
    snprintf(path, sizeof path, "%s%s", ark_config.arkpath, ARK_SETTINGS);
    int missing = 0;
    char *text = safe_read_text(path, 256 * 1024, &missing);
    if (text || !missing) return text;
    text = safe_read_text("flash1:/" ARK_SETTINGS, 256 * 1024, &missing);
    return text ? text : calloc(1, 1);
}

int platform_ark_write_text(const char *text) {
    char path[160];
    snprintf(path, sizeof path, "%s%s", ark_config.arkpath, ARK_SETTINGS);
    int rc = safe_write_text(path, text);
    if (rc == 0) safe_write_text(NEMO_ARK_BACKUP, text);   // restored if an ARK update resets the file
    return rc;
}

static int rmtree_depth(const char *path, int depth) {
    if (depth > 6) return -1;
    SceUID d = sceIoDopen(path);
    if (d < 0) return sceIoRemove(path) < 0 ? -1 : 0;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    int rc = 0;
    char child[320];
    while (sceIoDread(d, &e) > 0) {
        if (strcmp(e.d_name, ".") && strcmp(e.d_name, "..")) {
            snprintf(child, sizeof child, "%s/%s", path, e.d_name);
            if (FIO_S_ISDIR(e.d_stat.st_mode)) rc |= rmtree_depth(child, depth + 1);
            else if (sceIoRemove(child) < 0) rc = -1;
        }
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    if (sceIoRmdir(path) < 0) rc = -1;
    return rc;
}

int platform_rmtree(const char *path) { return rmtree_depth(path, 0); }

void platform_sync_card(void) {
    int a = sceIoSync("ms0:", 0), b = sceIoSync("fatms0:", 0);
    log_msg("card: sync ms0=%08X fatms0=%08X", (unsigned)a, (unsigned)b);
}

int platform_copy_file(const char *from, const char *to) {
    SceUID in = sceIoOpen(from, PSP_O_RDONLY, 0);
    if (in < 0) return -1;
    SceUID out = sceIoOpen(to, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) { sceIoClose(in); return -1; }
    static char buf[64 * 1024];
    int n, rc = 0;
    while ((n = sceIoRead(in, buf, sizeof buf)) > 0)
        if (sceIoWrite(out, buf, n) != n) { rc = -1; break; }
    if (n < 0) rc = -1;
    sceIoClose(out);
    sceIoClose(in);
    return rc;
}

long platform_file_size(const char *path) {
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    return (long)st.st_size;
}

long platform_dir_size(const char *dir) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return 0;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    long total = 0;
    while (sceIoDread(d, &e) > 0) {
        if (!FIO_S_ISDIR(e.d_stat.st_mode)) total += (long)e.d_stat.st_size;
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    return total;
}

int platform_pbp_title(const char *pbp_path, char *out, size_t cap) {
    FILE *f = fopen(pbp_path, "rb");
    if (!f) return -1;
    PbpHeader h;
    size_t size = 0;
    uint8_t *sfo = pbp_read_header(f, &h) == 0 ? pbp_read_section(f, &h, PBP_PARAM_SFO, &size) : NULL;
    fclose(f);
    int rc = sfo ? sfo_get_string(sfo, size, "TITLE", out, cap) : -1;
    free(sfo);
    return rc;
}

int platform_ark_get(const char *key, int default_on) {
    char path[160];
    snprintf(path, sizeof path, "%s%s", ark_config.arkpath, ARK_SETTINGS);
    return arksettings_read_key_file(path, "flash1:/" ARK_SETTINGS, key, default_on);
}

int platform_ark_set(const char *key, int on, int default_on) {
    char path[160];
    snprintf(path, sizeof path, "%s%s", ark_config.arkpath, ARK_SETTINGS);
    return arksettings_update_key_file(path, "flash1:/" ARK_SETTINGS, key, on, default_on);
}

// The newlib heap is one block taken at start (PSP_HEAP_SIZE_KB(-1)); find how much of it sbrk has not
// handed to malloc yet by probing sbrk up and giving it straight back.
static uint32_t heap_cap, kfree_start;

void platform_memory_init(void) {
    uint32_t lo = 0, hi = 64u * 1024 * 1024;
    while (hi - lo > 4096) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (sbrk((intptr_t)mid) != (void *)-1) {
            sbrk(-(intptr_t)mid);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    heap_cap = (uint32_t)mallinfo().arena + lo;
    kfree_start = (uint32_t)sceKernelTotalFreeMemSize();
}

void platform_memory(uint32_t *free_bytes, uint32_t *total_bytes) {
    struct mallinfo mi = mallinfo();
    uint32_t used = (uint32_t)mi.uordblks;
    uint32_t kfree = (uint32_t)sceKernelTotalFreeMemSize();
    *free_bytes = (heap_cap > used ? heap_cap - used : 0) + kfree;
    *total_bytes = heap_cap ? heap_cap + kfree_start : 0;
}
