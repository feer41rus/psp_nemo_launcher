#include "gamesize.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

const char *game_format_label(const Game *g) {
    switch (g->format) {
    case GF_ISO: {
        size_t n = strlen(g->path);
        return n > 4 && strcasecmp(g->path + n - 4, ".cso") == 0 ? "CSO" : "ISO";
    }
    case GF_PBP_PSN: return "PSN";
    case GF_PBP_PS1: return "PS1 EBOOT";
    case GF_PBP_HOMEBREW: return "EBOOT";
    case GF_UMD: return "UMD";
    }
    return "";
}

static long long tree(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    long long sum = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 8) sum += tree(p, depth + 1); }
        else sum += st.st_size;
    }
    closedir(d);
    return sum;
}

// The folder holding an EBOOT.PBP path.
static long long folder_of(const char *path) {
    char dir[256];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return 0;
    *slash = 0;
    return tree(dir, 0);
}

long long game_size(const Game *g) {
    if (g->format == GF_UMD) return -1;
    if (g->format == GF_ISO) {
        struct stat st;
        return stat(g->path, &st) == 0 ? (long long)st.st_size : -1;
    }
    if (g->disc_count > 1) {   // multi-disc PS1: every disc's folder
        long long sum = 0;
        for (int i = 0; i < g->disc_count && i < GAME_MAX_DISCS; i++) sum += folder_of(g->discs[i].path);
        return sum;
    }
    return folder_of(g->path);
}

void game_size_text(char *out, size_t cap, long long bytes, int lang_en) {
    long long mb10 = (bytes * 10 + 512 * 1024) / (1024 * 1024);   // tenths of a MB
    if (bytes < 10LL * 1024 * 1024) {   // under 10 MB: one decimal
        snprintf(out, cap, lang_en ? "%lld.%lld MB" : "%lld,%lld МБ", mb10 / 10, mb10 % 10);
        return;
    }
    long long mb = (bytes + 512 * 1024) / (1024 * 1024);
    if (mb < 1024) {
        snprintf(out, cap, lang_en ? "%lld MB" : "%lld МБ", mb);
        return;
    }
    long long gb10 = (bytes * 10 + 512LL * 1024 * 1024) / (1024LL * 1024 * 1024);
    snprintf(out, cap, lang_en ? "%lld.%lld GB" : "%lld,%lld ГБ", gb10 / 10, gb10 % 10);
}
