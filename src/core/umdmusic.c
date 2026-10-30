#include "umdmusic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void lower(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

static int by_name(const void *a, const void *b) { return strcasecmp(*(const char *const *)a, *(const char *const *)b); }

int umdmusic_attach_ref(MusicLib *l, const char *title, const char *title_fmt, const char *dir, const char *const *files,
                        int n, UmdMusicRef *r) {
    r->first_track = r->album = -1;
    r->prev_tracks = l->track_count;
    r->prev_albums = l->album_count;
    if (n <= 0 || !files) return -1;
    const char **sorted = malloc(sizeof *sorted * (size_t)n);
    Track *t = realloc(l->tracks, sizeof *t * (size_t)(l->track_count + n));
    if (!sorted || !t) { free(sorted); if (t) l->tracks = t; return -1; }
    l->tracks = t;
    if (l->track_cap < l->track_count + n) l->track_cap = l->track_count + n;
    Album *al = realloc(l->albums, sizeof *al * (size_t)(l->album_count + 1));
    if (!al) { free(sorted); return -1; }
    l->albums = al;
    if (l->album_cap < l->album_count + 1) l->album_cap = l->album_count + 1;
    memcpy(sorted, files, sizeof *sorted * (size_t)n);
    qsort(sorted, (size_t)n, sizeof *sorted, by_name);
    Album *a = &l->albums[l->album_count];
    memset(a, 0, sizeof *a);
    snprintf(a->title, sizeof a->title, "UMD: %s", title);
    a->first = l->track_count;
    a->count = n;
    snprintf(a->folder, sizeof a->folder, "%s", dir);
    for (int i = 0; i < n; i++) {
        Track *k = &l->tracks[l->track_count + i];
        memset(k, 0, sizeof *k);
        snprintf(k->path, sizeof k->path, "%s/%s", dir, sorted[i]);
        snprintf(k->id, sizeof k->id, "%s", k->path);
        lower(k->id);
        snprintf(k->title, sizeof k->title, title_fmt, i + 1);
        snprintf(k->album, sizeof k->album, "%s", a->title);
        k->track_no = i + 1;
        k->format = AF_UNKNOWN;
    }
    free(sorted);
    r->first_track = l->track_count;
    r->album = l->album_count;
    l->track_count += n;
    l->album_count++;
    return 0;
}

void umdmusic_detach(MusicLib *l, UmdMusicRef *r) {
    if (r->first_track < 0) return;
    if (l->track_count > r->prev_tracks) l->track_count = r->prev_tracks;
    if (l->album_count > r->prev_albums) l->album_count = r->prev_albums;
    r->first_track = r->album = -1;
}

int umdmusic_is_track(const Track *t) { return !strncasecmp(t->path, "disc0:/", 7); }

int umdmusic_is_stream(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".MPS");
}
