#include "log.h"
#include "platform.h"
#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

static FILE *log_file;
static uint32_t start_ms;
static SceUID log_lock = -1;

void log_open(void) {
    mkdir("ms0:/PSP/NEMO", 0777);
    remove(NEMO_LOG_PREV_PATH);
    rename(NEMO_LOG_PATH, NEMO_LOG_PREV_PATH);
    log_file = fopen(NEMO_LOG_PATH, "w");
    start_ms = platform_ms();
    log_lock = sceKernelCreateSema("nemo_log", 0, 1, 1, NULL);
}

void log_reopen(void) {
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
