#include "mviews.h"
#include "utf8.h"
#include "sjis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- artists ----------

static const MusicLib *sort_lib;   // qsort context (single-threaded index build)

static int cmp_album_artist(const void *pa, const void *pb) {
    const Album *x = &sort_lib->albums[*(const int *)pa], *y = &sort_lib->albums[*(const int *)pb];
    int ux = x->artist[0] == 0, uy = y->artist[0] == 0;
    if (ux != uy) return ux - uy;                   // unknown artist last
    int c = utf8_casecmp(x->artist, y->artist);
    if (c) return c;
    if (x->year != y->year) return y->year - x->year;   // newest first
    return *(const int *)pa - *(const int *)pb;
}

int artists_build(ArtistIndex *ix, const MusicLib *lib) {
    memset(ix, 0, sizeof *ix);
    int n = lib->album_count;
    if (n == 0) return 0;
    ix->albums = malloc((size_t)n * sizeof *ix->albums);
    ix->artists = malloc((size_t)n * sizeof *ix->artists);
    if (!ix->albums || !ix->artists) {
        artists_free(ix);
        return -1;
    }
    for (int i = 0; i < n; i++) ix->albums[i] = i;
    sort_lib = lib;
    qsort(ix->albums, (size_t)n, sizeof *ix->albums, cmp_album_artist);
    for (int i = 0; i < n; i++) {
        const Album *a = &lib->albums[ix->albums[i]];
        MArtist *cur = ix->count ? &ix->artists[ix->count - 1] : NULL;
        if (!cur || utf8_casecmp(cur->name, a->artist) != 0) {
            cur = &ix->artists[ix->count++];
            cur->name = a->artist;
            cur->album_first = i;
            cur->album_count = cur->track_count = 0;
        }
        cur->album_count++;
        cur->track_count += a->count;
    }
    return 0;
}

void artists_free(ArtistIndex *ix) {
    free(ix->artists);
    free(ix->albums);
    memset(ix, 0, sizeof *ix);
}

int artists_tracks(const ArtistIndex *ix, const MusicLib *lib, int artist, int *out, int max) {
    if (artist < 0 || artist >= ix->count) return 0;
    const MArtist *ar = &ix->artists[artist];
    int n = 0;
    for (int i = 0; i < ar->album_count; i++) {
        const Album *a = &lib->albums[ix->albums[ar->album_first + i]];
        for (int k = 0; k < a->count && n < max; k++) out[n++] = a->first + k;
    }
    return n;
}

// ---------- folders ----------

static int folder_add(FolderTree *t, int *cap, const char *path, size_t len, int parent) {
    if (t->count == *cap) {
        int ncap = *cap ? *cap * 2 : 16;
        MFolder *nf = realloc(t->folders, (size_t)ncap * sizeof *nf);
        if (!nf) return -1;
        t->folders = nf;
        *cap = ncap;
    }
    MFolder *f = &t->folders[t->count];
    memcpy(f->path, path, len);
    f->path[len] = 0;
    const char *last = strrchr(f->path, '/');
    if (parent < 0) snprintf(f->name, sizeof f->name, "MUSIC");
    else sjis_to_utf8(last ? last + 1 : f->path, f->name, sizeof f->name);
    f->parent = parent;
    f->direct = f->total = 0;
    return t->count++;
}

static int folder_find_len(const FolderTree *t, const char *path, size_t len) {
    for (int i = 0; i < t->count; i++)
        if (strlen(t->folders[i].path) == len && memcmp(t->folders[i].path, path, len) == 0) return i;
    return -1;
}

// Folder for path[0..len) and its missing ancestors down from the root; -1 out of memory.
static int folder_ensure(FolderTree *t, int *cap, const char *path, size_t len, size_t root_len) {
    int f = folder_find_len(t, path, len);
    if (f >= 0) return f;
    size_t cut = len;
    while (cut > root_len && path[cut - 1] != '/') cut--;
    int parent = folder_ensure(t, cap, path, cut - 1, root_len);
    return parent < 0 ? -1 : folder_add(t, cap, path, len, parent);
}

int folders_build(FolderTree *t, const MusicLib *lib, const char *root) {
    memset(t, 0, sizeof *t);
    int cap = 0;
    size_t rl = strlen(root);
    if (rl >= sizeof t->folders[0].path || folder_add(t, &cap, root, rl, -1) < 0) return -1;
    for (int i = 0; i < lib->track_count; i++) {
        const char *p = lib->tracks[i].path;
        const char *slash = strrchr(p, '/');
        if (strncmp(p, root, rl) != 0 || p[rl] != '/' || !slash) continue;
        int f = folder_ensure(t, &cap, p, (size_t)(slash - p), rl);
        if (f < 0) {
            folders_free(t);
            return -1;
        }
        t->folders[f].direct++;
        for (; f >= 0; f = t->folders[f].parent) t->folders[f].total++;
    }
    return 0;
}

void folders_free(FolderTree *t) {
    free(t->folders);
    memset(t, 0, sizeof *t);
}

const char *folders_name(const FolderTree *t, int f) {
    if (f <= 0 || f >= t->count) return "MUSIC";
    return t->folders[f].name;
}

int folders_children(const FolderTree *t, int f, int *out, int max) {
    int n = 0;
    for (int i = 1; i < t->count && n < max; i++) {
        if (t->folders[i].parent != f) continue;
        int k = n++;   // insertion sort by name
        while (k > 0 && utf8_casecmp(folders_name(t, out[k - 1]), folders_name(t, i)) > 0) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = i;
    }
    return n;
}

static int cmp_track_path(const void *pa, const void *pb) {
    return strcmp(sort_lib->tracks[*(const int *)pa].path, sort_lib->tracks[*(const int *)pb].path);
}

int folders_tracks(const FolderTree *t, const MusicLib *lib, int f, int recursive, int *out, int max) {
    if (f < 0 || f >= t->count) return 0;
    const char *dir = t->folders[f].path;
    size_t dl = strlen(dir);
    int n = 0;
    for (int i = 0; i < lib->track_count && n < max; i++) {
        const char *p = lib->tracks[i].path;
        if (strncmp(p, dir, dl) != 0 || p[dl] != '/') continue;
        if (!recursive && strchr(p + dl + 1, '/')) continue;
        out[n++] = i;
    }
    sort_lib = lib;
    qsort(out, (size_t)n, sizeof *out, cmp_track_path);
    return n;
}

int folders_find(const FolderTree *t, const char *path) { return folder_find_len(t, path, strlen(path)); }

static const MusicLib *sort_lib;

static int by_title(const void *x, const void *y) {
    const Track *a = &sort_lib->tracks[*(const int *)x], *b = &sort_lib->tracks[*(const int *)y];
    int c = utf8_casecmp(a->title, b->title);
    if (c == 0) c = utf8_casecmp(a->artist, b->artist);
    return c ? c : *(const int *)x - *(const int *)y;   // stable for equal names
}

int alltracks_build(const MusicLib *lib, int *out, int cap) {
    int n = lib->track_count;
    if (n <= 0 || cap <= 0) return 0;
    int *all = malloc(sizeof(int) * (size_t)n);
    if (!all) return 0;
    for (int i = 0; i < n; i++) all[i] = i;
    sort_lib = lib;
    qsort(all, (size_t)n, sizeof(int), by_title);
    int m = n < cap ? n : cap;
    memcpy(out, all, sizeof(int) * (size_t)m);
    free(all);
    return m;
}
