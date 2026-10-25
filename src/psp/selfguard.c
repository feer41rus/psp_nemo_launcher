#include "ui.h"
#include "gfx.h"
#include "gamefiles.h"
#include "log.h"
#include "platform.h"
#include "safefile.h"
#include "usb.h"
#include "version.h"
#include <SDL2/SDL.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Nemo instead of XMB (1.1.11): boot counter, a backup copy of Nemo as ARK's "Custom App"
// (ms0:/PSP/APP/CUSTOM/EBOOT.PBP) and restoring Nemo when an ARK update replaced VBOOT.PBP.
#define BOOT_FILE "ms0:/PSP/NEMO/boot.txt"
#define STAMP_FILE "ms0:/PSP/NEMO/backup.txt"
#define BACKUP_PBP "ms0:/PSP/APP/CUSTOM/EBOOT.PBP"
#define HEALTHY_MS 15000
#define BACKUP_AFTER_MS 10000

static char run_path[256];
static uint32_t start_ms;
static int healthy_done, backup_state, guard_toast;   // backup: 0 wait, 1 running/done, -1 not needed
static volatile int backup_finished;

static int read_int_file(const char *path) {
    int missing;
    char *t = safe_read_text(path, 64, &missing);
    int v = t ? atoi(t) : 0;
    free(t);
    return v;
}

static void write_int_file(const char *path, int v) {
    char s[16];
    snprintf(s, sizeof s, "%d\n", v);
    safe_write_text(path, s);
}

static void stamp_text(char *out, size_t cap) { snprintf(out, cap, "%s %s %s\n", NEMO_VERSION, __DATE__, __TIME__); }

static int backup_thread(void *arg) {
    (void)arg;
    sceIoMkdir("ms0:/PSP/APP", 0777);
    sceIoMkdir("ms0:/PSP/APP/CUSTOM", 0777);
    int rc = platform_copy_file(run_path, BACKUP_PBP);
    log_msg("guard: backup copy to %s rc=%d", BACKUP_PBP, rc);
    if (rc == 0) {
        char s[96];
        stamp_text(s, sizeof s);
        safe_write_text(STAMP_FILE, s);
    }
    backup_finished = 1;
    return 0;
}

void selfguard_start(App *a, const char *path) {
    snprintf(run_path, sizeof run_path, "%s", path ? path : "");
    start_ms = platform_ms();
    int disable;
    int count = bootguard_on_start(read_int_file(BOOT_FILE), a->lib->settings.replace_xmb, &disable);
    write_int_file(BOOT_FILE, count);
    if (disable) {
        a->lib->settings.replace_xmb = 0;
        library_save(a->lib, NEMO_DATA_PATH);
        guard_toast = 1;
        log_msg("guard: two failed starts in a row, Nemo instead of XMB switched off");
    }
    // "launcher, on" keeps the PSP starting in Nemo; off = the XMB after a restart.
    platform_set_launcher_mode(a->lib->settings.replace_xmb);
    int from_backup = strstr(run_path, "/PSP/APP/CUSTOM/") != NULL;
    log_msg("guard: run from %s, boot count %d, replace_xmb %d", run_path, count, a->lib->settings.replace_xmb);
    if (from_backup) {
        char vboot[192], title[128] = "";
        snprintf(vboot, sizeof vboot, "%sVBOOT.PBP", platform_arkpath());
        // Only a readable launcher of another title counts: an unreadable VBOOT.PBP (damaged
        // card) must not trigger the restore, which deletes VBOOT_ARK.PBP.
        int rc = platform_pbp_title(vboot, title, sizeof title);
        if (rc != 0 || !title[0]) {
            log_msg("guard: VBOOT.PBP unreadable (rc=%d), restore not offered", rc);
        } else if (strcmp(title, "Nemo Launcher") != 0) {
            log_msg("guard: VBOOT.PBP is \"%s\", not Nemo: asking to restore", title);
            a->rx_stage = 1;
            a->rx_yes = 1;
        }
        backup_state = -1;                    // never copy the backup onto itself
    } else {
        int missing;
        char *old = safe_read_text(STAMP_FILE, 128, &missing);
        char now[96];
        stamp_text(now, sizeof now);
        backup_state = old && strcmp(old, now) == 0 && platform_file_size(BACKUP_PBP) > 0 ? -1 : 0;
        free(old);
    }
}

int selfguard_busy(void) { return backup_state == 1 && !backup_finished; }

void selfguard_clean_exit(void) {
    healthy_done = 1;
    write_int_file(BOOT_FILE, 0);
}

void selfguard_tick(App *a) {
    uint32_t up = platform_ms() - start_ms;
    if (guard_toast) { guard_toast = 0; ui_toast(a, tr(S_RX_GUARD)); }
    if (!healthy_done && up >= HEALTHY_MS && !usb_active()) {
        healthy_done = 1;
        write_int_file(BOOT_FILE, 0);
    }
    // Copy in the background, later and never while the PC holds the card or music was unloaded.
    if (backup_state == 0 && up >= BACKUP_AFTER_MS && !usb_active() && !a->media_suspended) {
        backup_state = 1;
        SDL_Thread *t = SDL_CreateThread(backup_thread, "nemo_backup", NULL);
        if (t) SDL_DetachThread(t);
    }
}

static void restore(App *a) {
    char vboot[192], ark[192];
    snprintf(vboot, sizeof vboot, "%sVBOOT.PBP", platform_arkpath());
    snprintf(ark, sizeof ark, "%sVBOOT_ARK.PBP", platform_arkpath());
    sceIoRemove(ark);                          // the new ARK menu replaces the old backup
    int rc = sceIoRename(vboot, ark);
    if (rc >= 0) rc = platform_copy_file(run_path, vboot);
    log_msg("guard: restore Nemo rc=%d", rc);
    if (rc < 0) { ui_toast(a, tr(S_RX_FAIL)); a->rx_stage = 0; return; }
    if (platform_file_size(NEMO_ARK_BACKUP) > 0) { a->rx_stage = 2; a->rx_yes = 1; }
    else { a->rx_stage = 0; ui_toast(a, tr(S_RX_DONE)); }
}

void selfguard_action(App *a, Action act) {
    if (act == ACT_LEFT || act == ACT_RIGHT) { a->rx_yes = !a->rx_yes; return; }
    if (act == ACT_BACK) { a->rx_stage = 0; return; }
    if (act != ACT_CONFIRM) return;
    if (a->rx_stage == 1) {
        if (a->rx_yes) restore(a);
        else a->rx_stage = 0;
    } else {
        if (a->rx_yes) {
            char path[192];
            snprintf(path, sizeof path, "%sSETTINGS.TXT", platform_arkpath());
            int rc = platform_copy_file(NEMO_ARK_BACKUP, path);
            log_msg("guard: restore SETTINGS.TXT rc=%d", rc);
        }
        a->rx_stage = 0;
        ui_toast(a, tr(S_RX_DONE));
    }
}

void selfguard_draw(const App *a) {
    if (!a->rx_stage) return;
    int w = 400, h = 110, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000b0);
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, COL_ACCENT);
    gfx_text_fit(FONT_BODY, x + 16, y + 18, w - 32, COL_TEXT, tr(a->rx_stage == 1 ? S_RX_RESTORE_Q : S_RX_SETTINGS_Q));
    const StrId B[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 38, on = (i == 0) == a->rx_yes;
        gfx_round_rect(bx, by, 104, 24, 12, on ? 0x2b5fa8ff : COL_PANEL);
        const char *t = tr(B[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}
