#include "fsbrowse.h"
#include "sjis.h"
#include "utf8.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

void fs_display(const char *name, char *out, size_t cap) { sjis_to_utf8(name, out, cap); }

static int cmp_entry(const void *pa, const void *pb) {
    const FsEntry *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
    char x[256 * 3 / 2 + 4], y[sizeof x];   // by letters: Shift-JIS Cyrillic is 2 bytes, UTF-8 too
    fs_display(a->name, x, sizeof x);
    fs_display(b->name, y, sizeof y);
    int c = utf8_casecmp(x, y);
    return c ? c : strcmp(a->name, b->name);
}

int fs_list(const char *dir, FsList *out) {
    memset(out, 0, sizeof *out);
    DIR *d = opendir(dir);
    if (!d) return -1;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (out->count >= FS_LIST_MAX) { out->truncated = 1; break; }
        if (out->count == cap) {
            int ncap = cap ? cap * 2 : 64;
            FsEntry *n = realloc(out->items, (size_t)ncap * sizeof *n);
            if (!n) break;
            out->items = n;
            cap = ncap;
        }
        FsEntry *it = &out->items[out->count];
        memset(it, 0, sizeof *it);
        utf8_copy(it->name, sizeof it->name, e->d_name);
        char p[FS_PATH_MAX + 260];
        struct stat st;
        if (fs_join(p, sizeof p, dir, e->d_name) == 0 && stat(p, &st) == 0) {
            it->is_dir = S_ISDIR(st.st_mode);
            it->size = it->is_dir ? 0 : (long long)st.st_size;
            it->mtime = (long)st.st_mtime;
        }
        out->count++;
    }
    closedir(d);
    if (out->count > 1) qsort(out->items, (size_t)out->count, sizeof *out->items, cmp_entry);
    return 0;
}

void fs_list_free(FsList *l) {
    free(l->items);
    memset(l, 0, sizeof *l);
}

int fs_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t n = strlen(dir);
    int w = snprintf(out, cap, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", name);
    if (w < 0 || (size_t)w >= cap) {
        if (cap) out[0] = '\0';
        return -1;
    }
    return 0;
}

// Length of "ms0:/" in path, 0 if it has no device.
static size_t root_len(const char *path) {
    const char *c = strstr(path, ":/");
    return c ? (size_t)(c - path) + 2 : 0;
}

int fs_is_root(const char *path) {
    size_t r = root_len(path);
    return r && path[r] == '\0';
}

int fs_parent(char *path) {
    size_t r = root_len(path), n = strlen(path);
    if (!r || n <= r) return 0;
    while (n > r && path[n - 1] == '/') n--;
    while (n > r && path[n - 1] != '/') n--;
    if (n > r) n--;   // drop the slash, but keep the root's
    path[n] = '\0';
    return 1;
}

const char *fs_name(const char *path) {
    if (fs_is_root(path)) return "";
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int fs_same_device(const char *a, const char *b) {
    size_t ra = root_len(a), rb = root_len(b);
    return ra == rb && strncasecmp(a, b, ra) == 0;
}

int fs_inside(const char *dir, const char *path) {
    size_t n = strlen(dir);
    while (n && dir[n - 1] == '/') n--;
    if (strncasecmp(dir, path, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

// The path after "dev:/", compared without case.
static const char *rel(const char *path) { return path + root_len(path); }

// rel is the folder f, or f/..., or (when parents count) a folder that holds f.
static int touches(const char *r, const char *f, int parents) {
    size_t n = strlen(r), fl = strlen(f);
    if (n >= fl) return strncasecmp(r, f, fl) == 0 && (r[fl] == '\0' || r[fl] == '/');
    return parents && n && strncasecmp(r, f, n) == 0 && f[n] == '/';
}

int fs_protected(const char *path) {
    static const char *const DIRS[] = { "PSP/SAVEDATA/ARK_01234", "SEPLUGINS", "PSP/NEMO" };
    const char *r = rel(path);
    for (size_t i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++)
        if (touches(r, DIRS[i], 1)) return 1;
    return 0;
}

int fs_is_textfile(const char *name) {
    static const char *const EXT[] = { ".json", ".txt", ".ini", ".cfg", ".xml", ".log", ".m3u", ".md" };
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof EXT / sizeof EXT[0]; i++) {
        size_t e = strlen(EXT[i]);
        if (n > e && strcasecmp(name + n - e, EXT[i]) == 0) return 1;
    }
    return 0;
}

int fs_game_path(const char *path) {
    const char *r = rel(path);
    if (!*r) return 1;
    return touches(r, "ISO", 1) || touches(r, "PSP/GAME", 1);
}
