#include "logrot.h"
#include <stdio.h>

const int LOG_KEEP_CHOICES[LOG_KEEP_CHOICE_COUNT] = { 0, 2, 5, 10, 20 };

int logrot_valid_keep(int keep) {
    for (int i = 0; i < LOG_KEEP_CHOICE_COUNT; i++)
        if (LOG_KEEP_CHOICES[i] == keep) return keep;
    return LOG_KEEP_DEFAULT;
}

void logrot_path(char *out, size_t cap, const char *dir, int i) {
    if (i == 0) snprintf(out, cap, "%s/nemo.log", dir);
    else snprintf(out, cap, "%s/nemo.%d.log", dir, i);
}

void logrot_rotate(const char *dir, int keep) {
    if (keep <= 0) return;
    char from[256], to[256];
    snprintf(from, sizeof from, "%s/nemo.prev.log", dir);
    remove(from);
    for (int i = keep - 1; i <= LOG_KEEP_MAX; i++) {   // runs beyond the new limit
        logrot_path(from, sizeof from, dir, i);
        remove(from);
    }
    for (int i = keep - 2; i >= 0; i--) {
        logrot_path(from, sizeof from, dir, i);
        logrot_path(to, sizeof to, dir, i + 1);
        rename(from, to);
    }
}
