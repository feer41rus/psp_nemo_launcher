#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SIDE_PATH 600

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int side_paths(const char *path, char *tmp, char *bak) {
    return snprintf(tmp, SIDE_PATH, "%s.tmp", path) < SIDE_PATH &&
           snprintf(bak, SIDE_PATH, "%s.bak", path) < SIDE_PATH;
}

// Finish or roll back an interrupted safe_write_text.
static void recover(const char *path) {
    char tmp[SIDE_PATH], bak[SIDE_PATH];
    if (!side_paths(path, tmp, bak)) return;
    if (!exists(path)) {
        // .tmp is only renamed into place after it was written completely: prefer it.
        if (exists(tmp) && rename(tmp, path) == 0) remove(bak);
        else if (exists(bak)) rename(bak, path);
    } else {
        remove(tmp);   // a write that never reached the swap
        remove(bak);   // a swap that finished but was not cleaned up
    }
}

char *safe_read_text(const char *path, size_t max_size, int *missing) {
    *missing = 0;
    recover(path);
    struct stat st;
    if (stat(path, &st) != 0) {
        *missing = 1;
        return NULL;
    }
    if (!S_ISREG(st.st_mode) || (size_t)st.st_size > max_size) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = (size_t)st.st_size;
    char *buf = malloc(n + 1);
    if (buf && fread(buf, 1, n, f) != n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) buf[n] = '\0';
    return buf;
}

int safe_write_text(const char *path, const char *text) {
    char tmp[SIDE_PATH], bak[SIDE_PATH];
    if (!side_paths(path, tmp, bak)) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    int had_original = exists(path);
    remove(bak);
    if (had_original && rename(path, bak) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        if (had_original) rename(bak, path);   // put the original back
        remove(tmp);
        return -1;
    }
    remove(bak);
    return 0;
}
