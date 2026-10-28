#include "launch.h"
#include "blockdev.h"
#include "iso9660.h"
#include "platform.h"
#include <pspkernel.h>
#include <pspumd.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Runlevels and boot drivers, as used by arkMenu (include/entry.h).
#define HOMEBREW_RUNLEVEL  0x141
#define ISO_RUNLEVEL       0x123
#define ISO_PBOOT_RUNLEVEL 0x124
#define POPS_RUNLEVEL      0x144
#define PSN_DRIVER 0
#define ISO_DRIVER 3

#define UMD_EBOOT_BIN "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"
#define UMD_BOOT_BIN  "disc0:/PSP_GAME/SYSDIR/BOOT.BIN"
#define UMD_EBOOT_OLD "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD"

static char launch_path[256];
static char boot_path[256];

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void exec_param(struct SceKernelLoadExecVSHParam *p, const char *key, const char *argp) {
    memset(p, 0, sizeof *p);
    p->size = sizeof *p;
    p->key = (char *)key;
    p->argp = (void *)argp;
    p->args = strlen(argp) + 1;
}

// Which executable inside an ISO to boot (patched EBOOT.OLD, plain BOOT.BIN, or EBOOT.BIN).
static const char *iso_boot_file(const char *iso) {
    const char *result = UMD_EBOOT_BIN;
    BlockDev *d = blockdev_open(iso);
    if (!d) return result;
    uint32_t size;
    uint8_t magic[4] = { 0 };
    if (iso_stat(d, "PSP_GAME/SYSDIR/EBOOT.OLD", &size) == 0) {
        result = UMD_EBOOT_OLD;
    } else if (iso_read_prefix(d, "PSP_GAME/SYSDIR/BOOT.BIN", magic, 4) == 4 &&
               (magic[0] | magic[1] | magic[2] | magic[3]) != 0) {
        result = UMD_BOOT_BIN;
    }
    blockdev_close(d);
    return result;
}

void launch_mark_away(int away) {
    if (!away) { remove(LAUNCH_AWAY_FILE); return; }
    FILE *f = fopen(LAUNCH_AWAY_FILE, "w");
    if (f) { fputs("away\n", f); fclose(f); }
}

int launch_game(const Game *g, int disc) {
    const char *path = g->path;
    if (g->disc_count > 0 && disc >= 1 && disc <= g->disc_count) path = g->discs[disc - 1].path;
    snprintf(launch_path, sizeof launch_path, "%s", path);

    // ARK only returns to VBOOT.PBP after the game if launcher mode is on during the game.
    if (platform_set_launcher_mode(1) != 0) return -1;
    launch_mark_away(1);

    struct SceKernelLoadExecVSHParam param;
    int rc;
    switch (g->format) {
    case GF_ISO:
        snprintf(boot_path, sizeof boot_path, "%s", iso_boot_file(launch_path));
        exec_param(&param, "umdemu", boot_path);
        sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
        sctrlSESetBootConfFileIndex(ISO_DRIVER);
        sctrlSESetUmdFile(launch_path);
        rc = sctrlKernelLoadExecVSHWithApitype(ISO_RUNLEVEL, launch_path, &param);
        break;
    case GF_PBP_PSN: {
        int runlevel = ISO_RUNLEVEL;
        snprintf(boot_path, sizeof boot_path, "%s", UMD_EBOOT_BIN);
        char *slash = strrchr(launch_path, '/');
        if (slash) {
            char pboot[256];
            snprintf(pboot, sizeof pboot, "%.*sPBOOT.PBP", (int)(slash - launch_path + 1), launch_path);
            if (file_exists(pboot)) {
                snprintf(boot_path, sizeof boot_path, "%s", pboot);
                runlevel = ISO_PBOOT_RUNLEVEL;
            }
        }
        exec_param(&param, "umdemu", boot_path);
        sctrlSESetBootConfFileIndex(PSN_DRIVER);
        sctrlSESetUmdFile("");
        rc = sctrlKernelLoadExecVSHWithApitype(runlevel, launch_path, &param);
        break;
    }
    case GF_PBP_PS1:
        exec_param(&param, "pops", launch_path);
        rc = sctrlKernelLoadExecVSHWithApitype(POPS_RUNLEVEL, launch_path, &param);
        break;
    case GF_UMD: {   // the disc in the drive, as ARK's own launcher starts it (arkMenu umd.cpp, GPL)
        int apitype = 0x120;
        char update[64];
        snprintf(update, sizeof update, "ms0:/PSP/GAME/%s/PBOOT.PBP", g->disc_id);
        if (g->disc_id[0] && file_exists(update)) {
            apitype = 0x160;
            snprintf(launch_path, sizeof launch_path, "%s", update);
        } else snprintf(launch_path, sizeof launch_path, "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
        exec_param(&param, "game", launch_path);
        sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
        sctrlSESetBootConfFileIndex(MODE_UMD);
        sctrlSESetUmdFile("");
        rc = sctrlKernelLoadExecVSHWithApitype(apitype, launch_path, &param);
        break;
    }
    default:
        exec_param(&param, "game", launch_path);
        rc = sctrlKernelLoadExecVSHWithApitype(HOMEBREW_RUNLEVEL, launch_path, &param);
        break;
    }
    // Only reached when the firmware refused to start the game.
    launch_mark_away(0);
    platform_set_launcher_mode(0);
    return rc < 0 ? rc : -1;
}

int launch_exit_to_xmb(void) {
    // If "launcher, on" cannot be cleared, exiting would boot straight back into Nemo.
    if (platform_set_launcher_mode(0) != 0) return -1;
    launch_mark_away(1);
    sctrlKernelExitVSH(NULL);
    return -1;
}

int launch_self(void) {
    snprintf(launch_path, sizeof launch_path, "%sVBOOT.PBP", platform_arkpath());
    struct SceKernelLoadExecVSHParam param;
    exec_param(&param, "game", launch_path);
    launch_mark_away(1);
    sctrlKernelLoadExecVSHWithApitype(HOMEBREW_RUNLEVEL, launch_path, &param);
    launch_mark_away(0);
    return -1;
}

int launch_ark_menu(void) {
    snprintf(launch_path, sizeof launch_path, "%sVBOOT_ARK.PBP", platform_arkpath());
    if (!file_exists(launch_path)) return -1;
    if (platform_set_launcher_mode(0) != 0) return -2;
    struct SceKernelLoadExecVSHParam param;
    exec_param(&param, "game", launch_path);
    launch_mark_away(1);
    sctrlKernelLoadExecVSHWithApitype(HOMEBREW_RUNLEVEL, launch_path, &param);
    launch_mark_away(0);
    return -1;
}
