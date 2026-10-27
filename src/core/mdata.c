#include "mdata.h"
#include "cJSON.h"
#include "safefile.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MDATA_VERSION 1

static void plist_clear(MPlaylist *p) {
    free(p->ids);
    p->ids = NULL;
    p->count = p->cap = 0;
}

void mdata_init(MusicData *d) { memset(d, 0, sizeof *d); }

void mdata_free(MusicData *d) {
    plist_clear(&d->favorites);
    plist_clear(&d->recent);
    plist_clear(&d->queue);
    plist_clear(&d->m3u_imported);
    for (int i = 0; i < d->list_count; i++) plist_clear(&d->lists[i]);
    memset(d, 0, sizeof *d);
}

int mplaylist_has(const MPlaylist *p, const char *id) {
    for (int i = 0; i < p->count; i++)
        if (strcmp(p->ids[i], id) == 0) return 1;
    return 0;
}

// Append without the duplicate check (the saved queue may contain a track twice).
static int plist_push(MPlaylist *p, const char *id) {
    if (p->count == p->cap) {
        int cap = p->cap ? p->cap * 2 : 16;
        char (*ids)[MD_ID_LEN] = realloc(p->ids, (size_t)cap * MD_ID_LEN);
        if (!ids) return -1;
        p->ids = ids;
        p->cap = cap;
    }
    utf8_copy(p->ids[p->count++], MD_ID_LEN, id);
    return 0;
}

int mplaylist_add(MPlaylist *p, const char *id) {
    if (mplaylist_has(p, id)) return 0;
    return plist_push(p, id);
}

static void plist_remove_at(MPlaylist *p, int i) {
    memmove(p->ids[i], p->ids[i + 1], (size_t)(p->count - i - 1) * MD_ID_LEN);
    p->count--;
}

void mplaylist_remove(MPlaylist *p, const char *id) {
    for (int i = 0; i < p->count; i++)
        if (strcmp(p->ids[i], id) == 0) { plist_remove_at(p, i); return; }
}

void mdata_touch_recent(MusicData *d, const char *id) {
    mplaylist_remove(&d->recent, id);
    if (d->recent.count >= MD_RECENT) d->recent.count = MD_RECENT - 1;   // drop the oldest
    if (plist_push(&d->recent, id) != 0) return;
    // plist_push appended at the end: rotate it to the front.
    char tmp[MD_ID_LEN];
    memcpy(tmp, d->recent.ids[d->recent.count - 1], MD_ID_LEN);
    memmove(d->recent.ids[1], d->recent.ids[0], (size_t)(d->recent.count - 1) * MD_ID_LEN);
    memcpy(d->recent.ids[0], tmp, MD_ID_LEN);
}

static int clean_name(char *dst, size_t cap, const char *src) {
    while (*src == ' ') src++;
    utf8_copy(dst, cap, src);
    size_t n = strlen(dst);
    while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
    return n > 0;
}

static int name_taken(const MusicData *d, const char *name, int except) {
    for (int i = 0; i < d->list_count; i++)
        if (i != except && utf8_casecmp(d->lists[i].name, name) == 0) return 1;
    return 0;
}

int mdata_playlist_create(MusicData *d, const char *name) {
    char clean[64];
    if (!clean_name(clean, sizeof clean, name) || d->list_count >= MD_MAX_PLAYLISTS) return -1;
    if (name_taken(d, clean, -1)) return -1;
    MPlaylist *p = &d->lists[d->list_count];
    memset(p, 0, sizeof *p);
    strcpy(p->name, clean);
    return d->list_count++;
}

int mdata_playlist_rename(MusicData *d, int idx, const char *name) {
    char clean[64];
    if (idx < 0 || idx >= d->list_count || !clean_name(clean, sizeof clean, name)) return -1;
    if (name_taken(d, clean, idx)) return -1;
    strcpy(d->lists[idx].name, clean);
    return 0;
}

void mdata_playlist_delete(MusicData *d, int idx) {
    if (idx < 0 || idx >= d->list_count) return;
    plist_clear(&d->lists[idx]);
    memmove(&d->lists[idx], &d->lists[idx + 1], (size_t)(d->list_count - idx - 1) * sizeof d->lists[0]);
    d->list_count--;
    memset(&d->lists[d->list_count], 0, sizeof d->lists[0]);
}

void mdata_set_queue(MusicData *d, const Queue *q, const MusicLib *lib, uint32_t position_ms) {
    d->queue.count = 0;
    for (int i = 0; i < q->count; i++) {
        int t = q->items[i];
        if (t >= 0 && t < lib->track_count) plist_push(&d->queue, lib->tracks[t].id);
    }
    d->queue_pos = q->pos;
    d->position_ms = position_ms;
    d->shuffle = q->shuffle;
    d->repeat = (int)q->repeat;
}

int mdata_restore_queue(const MusicData *d, const MusicLib *lib, Queue *q, uint32_t *position_ms) {
    static int tracks[QUEUE_MAX];
    int n = 0, cur = -1, cur_lost = 0;
    for (int i = 0; i < d->queue.count && n < QUEUE_MAX; i++) {
        int t = musiclib_find(lib, d->queue.ids[i]);
        if (t < 0) {
            if (i == d->queue_pos) cur_lost = 1;
            continue;
        }
        if (i == d->queue_pos || (cur_lost && cur < 0 && i > d->queue_pos)) cur = n;
        tracks[n++] = t;
    }
    if (n == 0) return -1;
    if (cur < 0) { cur = 0; cur_lost = 1; }
    int shuffle = q->shuffle;
    q->shuffle = 0;                 // keep the saved play order as the original order
    queue_set(q, tracks, n, cur);
    (void)shuffle;
    q->shuffle = d->shuffle ? 1 : 0;
    q->repeat = (RepeatMode)(d->repeat >= REPEAT_OFF && d->repeat <= REPEAT_ONE ? d->repeat : REPEAT_OFF);
    *position_ms = cur_lost ? 0 : d->position_ms;
    return 0;
}

// ---------- JSON ----------

static cJSON *plist_json(const MPlaylist *p) {
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < p->count; i++) cJSON_AddItemToArray(a, cJSON_CreateString(p->ids[i]));
    return a;
}

static void plist_from_json(MPlaylist *p, const cJSON *a) {
    const cJSON *it;
    cJSON_ArrayForEach(it, a) if (cJSON_IsString(it)) plist_push(p, it->valuestring);
}

char *mdata_render(const MusicData *d) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", MDATA_VERSION);
    cJSON_AddItemToObject(root, "favorites", plist_json(&d->favorites));
    cJSON_AddItemToObject(root, "recent", plist_json(&d->recent));
    cJSON *lists = cJSON_AddArrayToObject(root, "playlists");
    for (int i = 0; i < d->list_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", d->lists[i].name);
        cJSON_AddItemToObject(o, "tracks", plist_json(&d->lists[i]));
        cJSON_AddItemToArray(lists, o);
    }
    cJSON *st = cJSON_AddObjectToObject(root, "player");
    cJSON_AddItemToObject(st, "queue", plist_json(&d->queue));
    cJSON_AddNumberToObject(st, "pos", d->queue_pos);
    cJSON_AddNumberToObject(st, "position_ms", d->position_ms);
    cJSON_AddNumberToObject(st, "shuffle", d->shuffle);
    cJSON_AddNumberToObject(st, "repeat", d->repeat);
    cJSON_AddNumberToObject(root, "view", d->view);
    cJSON *eq = cJSON_AddObjectToObject(root, "eq");
    cJSON_AddNumberToObject(eq, "on", d->eq_on);
    cJSON_AddNumberToObject(eq, "preset", d->eq_preset);
    cJSON_AddItemToObject(eq, "custom", cJSON_CreateIntArray(d->eq_custom, EQ_BANDS));
    cJSON_AddItemToObject(root, "m3u_imported", plist_json(&d->m3u_imported));
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

int mdata_save(const MusicData *d, const char *path) {
    char *text = mdata_render(d);
    if (!text) return -1;
    int rc = safe_write_text(path, text);
    free(text);
    return rc;
}

int mdata_load(MusicData *d, const char *path) {
    mdata_free(d);
    int missing = 0;
    char *text = safe_read_text(path, 4 * 1024 * 1024, &missing);
    if (!text) return missing ? 0 : -1;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }
    plist_from_json(&d->favorites, cJSON_GetObjectItemCaseSensitive(root, "favorites"));
    plist_from_json(&d->recent, cJSON_GetObjectItemCaseSensitive(root, "recent"));
    if (d->recent.count > MD_RECENT) d->recent.count = MD_RECENT;
    const cJSON *pl;
    cJSON_ArrayForEach(pl, cJSON_GetObjectItemCaseSensitive(root, "playlists")) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(pl, "name");
        if (!cJSON_IsString(name)) continue;
        int idx = mdata_playlist_create(d, name->valuestring);
        if (idx >= 0) plist_from_json(&d->lists[idx], cJSON_GetObjectItemCaseSensitive(pl, "tracks"));
    }
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "player");
    if (cJSON_IsObject(st)) {
        plist_from_json(&d->queue, cJSON_GetObjectItemCaseSensitive(st, "queue"));
        const cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(st, "pos"))) d->queue_pos = v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(st, "position_ms"))) d->position_ms = (uint32_t)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(st, "shuffle"))) d->shuffle = v->valueint != 0;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(st, "repeat"))) d->repeat = v->valueint;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "view");
    if (cJSON_IsNumber(v) && v->valueint >= 0 && v->valueint <= 3) d->view = v->valueint;   // 3: all tracks (1.2.3)
    const cJSON *eq = cJSON_GetObjectItemCaseSensitive(root, "eq");
    if (cJSON_IsObject(eq)) {
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(eq, "on"))) d->eq_on = v->valueint != 0;
        v = cJSON_GetObjectItemCaseSensitive(eq, "preset");
        if (cJSON_IsNumber(v) && v->valueint >= 0 && v->valueint < EQP_COUNT) d->eq_preset = v->valueint;
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(eq, "custom");
        for (int i = 0; i < EQ_BANDS && cJSON_IsArray(c); i++) {
            const cJSON *g = cJSON_GetArrayItem(c, i);
            if (!cJSON_IsNumber(g)) continue;
            int x = g->valueint;
            d->eq_custom[i] = x > EQ_MAX_DB ? EQ_MAX_DB : x < -EQ_MAX_DB ? -EQ_MAX_DB : x;
        }
    }
    plist_from_json(&d->m3u_imported, cJSON_GetObjectItemCaseSensitive(root, "m3u_imported"));
    cJSON_Delete(root);
    return 0;
}
