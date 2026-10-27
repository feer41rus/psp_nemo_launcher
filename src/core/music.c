#include "music.h"
#include "cJSON.h"
#include "safefile.h"
#include "utf8.h"
#include "sjis.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MUSIC_MAX_DEPTH 4
#define MUSIC_CACHE_VERSION 2   // 2: titles/albums from Shift-JIS file names converted (1.2.1)

typedef struct {
    char path[256];
    long long size, mtime;
    Track track;
} MCacheEntry;

typedef struct {
    MCacheEntry *items;
    int count, cap;
} MCacheList;

typedef struct {
    MusicLib *lib;
    MCacheList old, seen;
    ScanStats *stats;
} MScan;

void musiclib_init(MusicLib *l) { memset(l, 0, sizeof *l); }

void musiclib_free(MusicLib *l) {
    free(l->tracks);
    free(l->albums);
    free(l->m3u);
    memset(l, 0, sizeof *l);
}

int musiclib_find(const MusicLib *l, const char *id) {
    for (int i = 0; i < l->track_count; i++)
        if (strcmp(l->tracks[i].id, id) == 0) return i;
    return -1;
}

static void *grow(void *items, int *cap, int count, size_t size) {
    if (count < *cap) return items;
    int ncap = *cap ? *cap * 2 : 64;
    void *n = realloc(items, (size_t)ncap * size);
    if (n) *cap = ncap;
    return n;
}

static Track *push_track(MusicLib *l) {
    Track *t = grow(l->tracks, &l->track_cap, l->track_count, sizeof *t);
    if (!t) return NULL;
    l->tracks = t;
    t = &l->tracks[l->track_count++];
    memset(t, 0, sizeof *t);
    return t;
}

static MCacheEntry *push_entry(MCacheList *l) {
    MCacheEntry *e = grow(l->items, &l->cap, l->count, sizeof *e);
    if (!e) return NULL;
    l->items = e;
    e = &l->items[l->count++];
    memset(e, 0, sizeof *e);
    return e;
}

void music_title_from_filename(const char *path, char *out, size_t cap) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    sjis_to_utf8(name, out, cap);   // the PSP lists names in Shift-JIS
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
    for (char *p = out; *p; p++)
        if (*p == '-' || *p == '_') *p = ' ';
}

// Basename of the directory containing path.
static void folder_name(const char *path, char *out, size_t cap) {
    const char *end = strrchr(path, '/');
    if (!end) { utf8_copy(out, cap, ""); return; }
    const char *start = end;
    while (start > path && start[-1] != '/') start--;
    size_t n = (size_t)(end - start);
    char tmp[256];
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    sjis_to_utf8(tmp, out, cap);
}

static void parse_track(Track *t, const char *path, AudioFormat fmt) {
    utf8_copy(t->path, sizeof t->path, path);
    size_t i = 0;
    for (; path[i] && i < sizeof t->id - 1; i++) t->id[i] = (char)tolower((unsigned char)path[i]);
    t->id[i] = '\0';
    t->format = fmt;
    TagInfo tag;
    tags_read(path, fmt, &tag);
    utf8_copy(t->title, sizeof t->title, tag.title);
    utf8_copy(t->artist, sizeof t->artist, tag.artist);
    utf8_copy(t->album, sizeof t->album, tag.album);
    utf8_copy(t->album_artist, sizeof t->album_artist, tag.album_artist);
    t->year = tag.year;
    t->track_no = tag.track_no;
    t->disc_no = tag.disc_no;
    t->cover_offset = tag.cover_offset;
    t->cover_size = tag.cover_size;
    if (!t->title[0]) music_title_from_filename(path, t->title, sizeof t->title);
    if (!t->album[0]) folder_name(path, t->album, sizeof t->album);
}

static int is_playlist_file(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".m3u") == 0 || strcasecmp(dot, ".m3u8") == 0);
}

static void add_m3u(MusicLib *l, const char *path) {
    if (strlen(path) >= sizeof l->m3u[0]) return;
    if (l->m3u_count == l->m3u_cap) {
        int cap = l->m3u_cap ? l->m3u_cap * 2 : 8;
        char (*m)[256] = realloc(l->m3u, (size_t)cap * sizeof *m);
        if (!m) return;
        l->m3u = m;
        l->m3u_cap = cap;
    }
    strcpy(l->m3u[l->m3u_count++], path);
}

static void visit(MScan *s, const char *path) {
    const char *name = strrchr(path, '/');
    if (is_playlist_file(name ? name + 1 : path)) {
        add_m3u(s->lib, path);
        return;
    }
    AudioFormat fmt = audio_format_from_name(name ? name + 1 : path);
    if (fmt == AF_UNKNOWN || strlen(path) >= sizeof s->lib->tracks[0].path) return;
    struct stat st;
    if (stat(path, &st) != 0) return;
    long long size = (long long)st.st_size, mtime = (long long)st.st_mtime;
    const MCacheEntry *hit = NULL;
    for (int i = 0; i < s->old.count; i++) {
        const MCacheEntry *e = &s->old.items[i];
        if (e->size == size && e->mtime == mtime && strcmp(e->path, path) == 0) { hit = e; break; }
    }
    MCacheEntry *rec = push_entry(&s->seen);
    if (!rec) return;
    if (hit) {
        *rec = *hit;
        if (s->stats) s->stats->reused++;
    } else {
        utf8_copy(rec->path, sizeof rec->path, path);
        rec->size = size;
        rec->mtime = mtime;
        parse_track(&rec->track, path, fmt);
        if (s->stats) s->stats->parsed++;
    }
    Track *t = push_track(s->lib);
    if (t) *t = rec->track;
}

static void walk(MScan *s, const char *dir, int depth) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *e;
    char path[512];
    while ((e = readdir(dp)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MUSIC_MAX_DEPTH) walk(s, path, depth + 1);
        } else {
            visit(s, path);
        }
    }
    closedir(dp);
}

static const char *album_artist_of(const Track *t) { return t->album_artist[0] ? t->album_artist : t->artist; }

static int same_album(const Track *x, const Track *y) {
    return utf8_casecmp(x->album, y->album) == 0 && utf8_casecmp(album_artist_of(x), album_artist_of(y)) == 0;
}

// Pass 1: group by album (case-insensitive incl. Cyrillic); path keeps it deterministic.
static int cmp_album(const void *a, const void *b) {
    const Track *x = a, *y = b;
    int c = utf8_casecmp(x->album, y->album);
    if (c) return c;
    c = utf8_casecmp(album_artist_of(x), album_artist_of(y));
    return c ? c : strcmp(x->path, y->path);
}

// Pass 2 (inside one album): disc, track, path — a total order (unknown discs were filled in).
static int cmp_in_album(const void *a, const void *b) {
    const Track *x = a, *y = b;
    if (x->disc_no != y->disc_no) return x->disc_no - y->disc_no;
    if (x->track_no != y->track_no) return x->track_no - y->track_no;
    return strcmp(x->path, y->path);
}

// When an album uses a single disc number, tracks without one belong to that disc
// (the author's album: every track says disc 3 except the outro).
static void fill_unknown_discs(Track *t, int n) {
    int disc = 0;
    for (int i = 0; i < n; i++) {
        if (!t[i].disc_no) continue;
        if (disc && t[i].disc_no != disc) return;   // several discs: leave unknown ones first
        disc = t[i].disc_no;
    }
    for (int i = 0; i < n; i++) if (!t[i].disc_no) t[i].disc_no = disc;
}

static void build_albums(MusicLib *l) {
    if (l->track_count > 1) qsort(l->tracks, (size_t)l->track_count, sizeof *l->tracks, cmp_album);
    for (int i = 0; i < l->track_count;) {
        int j = i + 1;
        while (j < l->track_count && same_album(&l->tracks[i], &l->tracks[j])) j++;
        fill_unknown_discs(&l->tracks[i], j - i);
        qsort(&l->tracks[i], (size_t)(j - i), sizeof *l->tracks, cmp_in_album);

        Album *a = grow(l->albums, &l->album_cap, l->album_count, sizeof *a);
        if (!a) return;
        l->albums = a;
        a = &l->albums[l->album_count++];
        memset(a, 0, sizeof *a);
        const Track *t = &l->tracks[i];
        utf8_copy(a->title, sizeof a->title, t->album);
        utf8_copy(a->artist, sizeof a->artist, album_artist_of(t));
        for (int k = i; k < j && !a->year; k++) a->year = l->tracks[k].year;
        a->first = i;
        a->count = j - i;
        utf8_copy(a->folder, sizeof a->folder, t->path);
        char *slash = strrchr(a->folder, '/');
        if (slash) *slash = '\0';
        i = j;
    }
}

// ---------- cache ----------

static const char *jstr(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double jnum(const cJSON *o, const char *k, int *ok) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(v)) { *ok = 0; return 0; }
    return v->valuedouble;
}

static void load_cache(const char *path, MCacheList *out) {
    int missing;
    char *text = safe_read_text(path, 8 * 1024 * 1024, &missing);
    if (!text) return;
    cJSON *root = cJSON_Parse(text);
    free(text);
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (cJSON_IsNumber(ver) && ver->valueint == MUSIC_CACHE_VERSION) {
        const cJSON *it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "tracks")) {
            const char *p = jstr(it, "path"), *title = jstr(it, "title"), *album = jstr(it, "album");
            const char *artist = jstr(it, "artist"), *aa = jstr(it, "album_artist");
            int ok = 1;
            long long size = (long long)jnum(it, "size", &ok), mtime = (long long)jnum(it, "mtime", &ok);
            int year = (int)jnum(it, "year", &ok), tn = (int)jnum(it, "track", &ok), dn = (int)jnum(it, "disc", &ok);
            int fmt = (int)jnum(it, "format", &ok);
            uint32_t co = (uint32_t)jnum(it, "cover_offset", &ok), cs = (uint32_t)jnum(it, "cover_size", &ok);
            if (!ok || !p || !title || !album || !artist || !aa || fmt < 0 || fmt >= AF_UNKNOWN) continue;
            MCacheEntry *e = push_entry(out);
            if (!e) break;
            utf8_copy(e->path, sizeof e->path, p);
            e->size = size;
            e->mtime = mtime;
            Track *t = &e->track;
            utf8_copy(t->path, sizeof t->path, p);
            size_t i = 0;
            for (; p[i] && i < sizeof t->id - 1; i++) t->id[i] = (char)tolower((unsigned char)p[i]);
            t->id[i] = '\0';
            utf8_copy(t->title, sizeof t->title, title);
            utf8_copy(t->album, sizeof t->album, album);
            utf8_copy(t->artist, sizeof t->artist, artist);
            utf8_copy(t->album_artist, sizeof t->album_artist, aa);
            t->year = year;
            t->track_no = tn;
            t->disc_no = dn;
            t->format = (AudioFormat)fmt;
            t->cover_offset = co;
            t->cover_size = cs;
        }
    }
    cJSON_Delete(root);
}

static void save_cache(const char *path, const MCacheList *l) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", MUSIC_CACHE_VERSION);
    cJSON *arr = cJSON_AddArrayToObject(root, "tracks");
    for (int i = 0; i < l->count; i++) {
        const MCacheEntry *e = &l->items[i];
        const Track *t = &e->track;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "path", e->path);
        cJSON_AddNumberToObject(o, "size", (double)e->size);
        cJSON_AddNumberToObject(o, "mtime", (double)e->mtime);
        cJSON_AddStringToObject(o, "title", t->title);
        cJSON_AddStringToObject(o, "album", t->album);
        cJSON_AddStringToObject(o, "artist", t->artist);
        cJSON_AddStringToObject(o, "album_artist", t->album_artist);
        cJSON_AddNumberToObject(o, "year", t->year);
        cJSON_AddNumberToObject(o, "track", t->track_no);
        cJSON_AddNumberToObject(o, "disc", t->disc_no);
        cJSON_AddNumberToObject(o, "format", t->format);
        cJSON_AddNumberToObject(o, "cover_offset", t->cover_offset);
        cJSON_AddNumberToObject(o, "cover_size", t->cover_size);
        cJSON_AddItemToArray(arr, o);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text) safe_write_text(path, text);
    free(text);
}

int music_scan(const char *root, const char *cache_path, MusicLib *lib, ScanStats *stats) {
    MScan s;
    memset(&s, 0, sizeof s);
    s.lib = lib;
    s.stats = stats;
    if (cache_path) load_cache(cache_path, &s.old);
    walk(&s, root, 0);
    int changed = !stats || stats->parsed > 0 || s.seen.count != s.old.count;
    if (cache_path && changed) save_cache(cache_path, &s.seen);
    free(s.old.items);
    free(s.seen.items);
    build_albums(lib);
    return lib->track_count;
}


int music_folder_cover(const char *folder, char *out, size_t cap) {
    static const char *const BASES[] = { "cover", "folder", "front" };
    DIR *dp = opendir(folder);
    if (!dp) return -1;
    int best = 3;
    char best_name[256] = "";
    struct dirent *e;
    while ((e = readdir(dp)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        if (strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg") && strcasecmp(dot, ".png")) continue;
        size_t base_len = (size_t)(dot - e->d_name);
        for (int i = 0; i < best; i++) {
            if (strlen(BASES[i]) == base_len && strncasecmp(e->d_name, BASES[i], base_len) == 0) {
                best = i;
                utf8_copy(best_name, sizeof best_name, e->d_name);
                break;
            }
        }
    }
    closedir(dp);
    if (best == 3) return -1;
    return snprintf(out, cap, "%s/%s", folder, best_name) < (int)cap ? 0 : -1;
}
