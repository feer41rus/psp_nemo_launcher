#include "gamefiles.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int starts_with_dev(const char *path, const char *rest) {
    return (strncasecmp(path, "ms0:", 4) == 0 || strncasecmp(path, "ef0:", 4) == 0) &&
           strncasecmp(path + 4, rest, strlen(rest)) == 0;
}

static int target_for(const Game *g, const char *path, GameTarget *t) {
    if (strstr(path, "..")) return -1;
    if (g->format == GF_ISO) {
        if (!starts_with_dev(path, "/ISO/") || strlen(path) <= 9) return -1;
        snprintf(t->path, sizeof t->path, "%s", path);
        t->is_dir = 0;
        return 0;
    }
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;
    size_t n = (size_t)(slash - path);
    if (n >= sizeof t->path) return -1;
    memcpy(t->path, path, n);
    t->path[n] = '\0';
    // The game's folder must sit inside PSP/GAME/, never be PSP/GAME itself.
    if (!starts_with_dev(t->path, "/PSP/GAME/") || strlen(t->path) <= strlen("ms0:/PSP/GAME/")) return -1;
    t->is_dir = 1;
    return 0;
}

int game_delete_targets(const Game *g, GameTarget *out, int max) {
    int n = 0;
    if (g->disc_count > 1) {
        for (int i = 0; i < g->disc_count && n < max; i++)
            if (target_for(g, g->discs[i].path, &out[n++]) != 0) return -1;
        return n;
    }
    if (max < 1 || target_for(g, g->path, &out[0]) != 0) return -1;
    return 1;
}

int save_game_code(const char *dir_name, char out[10]) {
    for (int i = 0; i < 9; i++) {
        char c = dir_name[i];
        if (i < 4 ? !isupper((unsigned char)c) : !isdigit((unsigned char)c)) return -1;
    }
    memcpy(out, dir_name, 9);
    out[9] = '\0';
    return 0;
}

int bootguard_on_start(int count, int replace_on, int *disable) {
    *disable = replace_on && count >= 2;
    return *disable ? 1 : count + 1;   // this start counts too
}
