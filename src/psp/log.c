#include "log.h"
#include "platform.h"
#include "logrot.h"
#include "photofile.h"
#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

static FILE *log_file;
static uint32_t start_ms;
static SceUID log_lock = -1;
static int log_enabled;

void log_open(int keep) {
    start_ms = platform_ms();
    log_lock = sceKernelCreateSema("nemo_log", 0, 1, 1, NULL);
    log_enabled = keep > 0;
    // The folders and the move happen even with the log off: the battery screen and cc.log need them (review M9).
    mkdir(NEMO_DATA_DIR, 0777);
    mkdir(NEMO_LOG_DIR, 0777);
    // Before 1.3.3 the logs lay in PSP/NEMO itself: they move into logs/ once (nemo*.log, cc.log).
    int moved = photofile_move_old(NEMO_DATA_DIR, NEMO_LOG_DIR, ".log", NULL);
    mkdir(NEMO_LOG_DIR "/battery", 0777);   // battery test logs too (the model stays in PSP/NEMO/battery)
    moved += photofile_move_old(NEMO_DATA_DIR "/battery", NEMO_LOG_DIR "/battery", ".csv", NULL);
    if (!log_enabled) return;
    logrot_rotate(NEMO_LOG_DIR, keep);
    log_file = fopen(NEMO_LOG_PATH, "w");
    if (moved) log_msg("log: %d old log file(s) moved into %s", moved, NEMO_LOG_DIR);
}

void log_disable(void) {
    log_pause();
    log_enabled = 0;
}

void log_reopen(void) {
    if (!log_enabled) return;
    if (log_lock >= 0) sceKernelWaitSema(log_lock, 1, NULL);
    if (log_file) fclose(log_file);
    log_file = fopen(NEMO_LOG_PATH, "a");
    if (log_lock >= 0) sceKernelSignalSema(log_lock, 1);
}

void log_pause(void) {
    if (log_lock >= 0) sceKernelWaitSema(log_lock, 1, NULL);
    if (log_file) fclose(log_file);
    log_file = NULL;
    if (log_lock >= 0) sceKernelSignalSema(log_lock, 1);
}

void log_resume(void) { log_reopen(); }

void log_msg(const char *fmt, ...) {
    if (!log_file) return;
    if (log_lock >= 0) sceKernelWaitSema(log_lock, 1, NULL);
    if (!log_file) {   // paused while we waited for the lock
        if (log_lock >= 0) sceKernelSignalSema(log_lock, 1);
        return;
    }
    fprintf(log_file, "[%7u] ", (unsigned)(platform_ms() - start_ms));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
    fflush(log_file);
    if (log_lock >= 0) sceKernelSignalSema(log_lock, 1);
}

volatile int log_verbose;
static volatile int hint_due, hint_given;

void log_set_verbose(int on) {
    log_verbose = on ? 1 : 0;
    log_msg("log: detailed log %s", on ? "on" : "off");
}

void log_fail(const char *fmt, ...) {
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    log_msg("FAIL: %s", b);
    if (!log_verbose && !hint_given) hint_due = 1;
}

int log_take_hint(void) {
    if (!hint_due || hint_given) return 0;
    hint_due = 0;
    hint_given = 1;
    return 1;
}
