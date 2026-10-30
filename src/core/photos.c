#include "photos.h"
#include "sjis.h"
#include "utf8.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

int photo_is_image(const char *name) {
    if (name[0] == '.') return 0;
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp");
}

// Case-insensitive strstr (folder names on FAT come in any case).
static const char *find_ci(const char *s, const char *needle) {
    size_t n = strlen(needle);
    for (; *s; s++)
        if (!strncasecmp(s, needle, n)) return s;
    return NULL;
}

PhotoSource photo_source(const char *path) {
    if (find_ci(path, "/PICTURE/Nemo/Games/")) return PS_GAMES;
    if (find_ci(path, "/PICTURE/Nemo/")) return PS_NEMO;
    if (find_ci(path, "/DCIM/")) return PS_CAMERA;
    return PS_UPLOADED;
}

static void push(PhotoList *l, const char *path, const struct stat *st) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        Photo *p = realloc(l->items, (size_t)cap * sizeof *p);
        if (!p) return;
        l->items = p;
        l->cap = cap;
    }
    size_t n = strlen(path);
    if (n >= sizeof l->items[0].path) return;
    Photo *p = &l->items[l->count++];
    memcpy(p->path, path, n + 1);
    p->size = (long long)st->st_size;
    p->mtime = (long)st->st_mtime;
    p->src = photo_source(path);
}

typedef void (*LeftFn)(const char *path, void *ctx);

// "<image>.tmp", "<image>.bak", "<image>.rot": what a rotation leaves behind when it is cut off.
static int is_leftover(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || (strcasecmp(dot, ".tmp") && strcasecmp(dot, ".bak") && strcasecmp(dot, ".rot"))) return 0;
    char base[256];
    size_t n = (size_t)(dot - name);
    if (n >= sizeof base) return 0;
    memcpy(base, name, n);
    base[n] = '\0';
    return photo_is_image(base);
}

static void walk(PhotoList *l, const char *dir, int depth, LeftFn left, void *ctx) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof l->items[0].path) continue;   // too long to keep
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < PHOTOS_MAX_DEPTH) walk(l, p, depth + 1, left, ctx);
        } else if (photo_is_image(e->d_name)) {
            push(l, p, &st);
        } else if (left && is_leftover(e->d_name)) {
            left(p, ctx);
        }
    }
    closedir(d);
}

static int newest_first(const void *a, const void *b) {
    const Photo *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime > y->mtime ? -1 : 1;
    return strcmp(x->path, y->path);
}

int photos_scan(PhotoList *l, const char *const *roots, int n_roots) { return photos_scan_ex(l, roots, n_roots, NULL, NULL); }

int photos_scan_ex(PhotoList *l, const char *const *roots, int n_roots, LeftFn left, void *ctx) {
    l->count = 0;
    for (int i = 0; i < n_roots; i++) walk(l, roots[i], 0, left, ctx);
    if (l->count > 1) qsort(l->items, (size_t)l->count, sizeof *l->items, newest_first);
    return l->count;
}

static int by_name(const void *a, const void *b) {
    const char *x = strrchr(((const Photo *)a)->path, '/'), *y = strrchr(((const Photo *)b)->path, '/');
    char ux[400], uy[400];   // card names are Shift-JIS: compare letters, as the file manager sorts
    sjis_to_utf8(x ? x + 1 : "", ux, sizeof ux);
    sjis_to_utf8(y ? y + 1 : "", uy, sizeof uy);
    int c = utf8_casecmp(ux, uy);
    return c ? c : strcmp(((const Photo *)a)->path, ((const Photo *)b)->path);
}

int photos_folder(PhotoList *l, const char *dir) {
    l->count = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    size_t n = strlen(dir);
    const char *sep = n && dir[n - 1] == '/' ? "" : "/";
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!photo_is_image(e->d_name)) continue;
        char p[600];
        struct stat st;
        snprintf(p, sizeof p, "%s%s%s", dir, sep, e->d_name);
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) push(l, p, &st);
    }
    closedir(d);
    if (l->count > 1) qsort(l->items, (size_t)l->count, sizeof *l->items, by_name);
    return l->count;
}

int photos_index_of(const PhotoList *l, const char *name) {
    for (int i = 0; i < l->count; i++) {
        const char *s = strrchr(l->items[i].path, '/');
        if (s && !strcmp(s + 1, name)) return i;
    }
    return 0;
}

int photos_remove(PhotoList *l, int index) {
    if (index < 0 || index >= l->count) return l->count ? (index >= l->count ? l->count - 1 : 0) : -1;
    memmove(&l->items[index], &l->items[index + 1], (size_t)(l->count - index - 1) * sizeof *l->items);
    l->count--;
    if (!l->count) return -1;
    return index < l->count ? index : l->count - 1;
}

void photos_free(PhotoList *l) {
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

void photo_thumb_key(const Photo *p, char *out, size_t cap) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a 64
    for (const char *s = p->path; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    long long v[2] = { p->size, (long long)p->mtime };
    const unsigned char *b = (const unsigned char *)v;
    for (size_t i = 0; i < sizeof v; i++) h = (h ^ b[i]) * 1099511628211ULL;
    snprintf(out, cap, "%016llx", (unsigned long long)h);
}
