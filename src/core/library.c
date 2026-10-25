#include "library.h"
#include "utf8.h"
#include "cJSON.h"
#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define LIB_FORMAT_VERSION 1

void library_settings_defaults(NemoSettings *s) {
    s->xmb_exit = 0;
    s->ncl_logo = 1;
    s->site = 1;
    s->show_music = 1;
    s->replace_xmb = 0;
}

void library_init(Library *l) {
    memset(l, 0, sizeof *l);
    strcpy(l->language, "ru");
    library_settings_defaults(&l->settings);
}

static void read_flag(const cJSON *o, const char *key, int *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsBool(v)) *out = cJSON_IsTrue(v);
}

// Copy a trimmed name; returns 0 if the result is empty.
static int clean_name(char *dst, size_t cap, const char *src) {
    while (*src == ' ') src++;
    utf8_copy(dst, cap, src);
    size_t n = strlen(dst);
    while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
    return n > 0;
}

static int folder_index(const Library *l, const char *name, int except) {
    for (int i = 0; i < l->folder_count; i++)
        if (i != except && utf8_casecmp(l->folders[i].name, name) == 0) return i;
    return -1;
}

int library_folder_create(Library *l, const char *name) {
    char clean[64];
    if (!clean_name(clean, sizeof clean, name) || l->folder_count >= LIB_MAX_FOLDERS) return -1;
    if (folder_index(l, clean, -1) >= 0) return -1;
    LibFolder *f = &l->folders[l->folder_count];
    memset(f, 0, sizeof *f);
    strcpy(f->name, clean);
    return l->folder_count++;
}

int library_folder_rename(Library *l, int idx, const char *name) {
    char clean[64];
    if (idx < 0 || idx >= l->folder_count || !clean_name(clean, sizeof clean, name)) return -1;
    if (folder_index(l, clean, idx) >= 0) return -1;
    strcpy(l->folders[idx].name, clean);
    return 0;
}

void library_folder_delete(Library *l, int idx) {
    if (idx < 0 || idx >= l->folder_count) return;
    memmove(&l->folders[idx], &l->folders[idx + 1],
            (size_t)(l->folder_count - idx - 1) * sizeof *l->folders);
    l->folder_count--;
}

int library_folder_has(const Library *l, int idx, const char *game_id) {
    if (idx < 0 || idx >= l->folder_count) return 0;
    const LibFolder *f = &l->folders[idx];
    for (int i = 0; i < f->count; i++)
        if (strcmp(f->ids[i], game_id) == 0) return 1;
    return 0;
}

int library_folder_add(Library *l, int idx, const char *game_id) {
    if (idx < 0 || idx >= l->folder_count) return -1;
    if (library_folder_has(l, idx, game_id)) return 0;
    LibFolder *f = &l->folders[idx];
    if (f->count >= LIB_MAX_IN_FOLDER) return -1;
    utf8_copy(f->ids[f->count++], LIB_ID_LEN, game_id);
    return 0;
}

void library_folder_remove(Library *l, int idx, const char *game_id) {
    if (idx < 0 || idx >= l->folder_count) return;
    LibFolder *f = &l->folders[idx];
    for (int i = 0; i < f->count; i++) {
        if (strcmp(f->ids[i], game_id) != 0) continue;
        memmove(f->ids[i], f->ids[i + 1], (size_t)(f->count - i - 1) * LIB_ID_LEN);
        f->count--;
        return;
    }
}

void library_touch_recent(Library *l, const char *game_id, int disc) {
    int pos = l->recent_count < LIB_MAX_RECENT ? l->recent_count : LIB_MAX_RECENT - 1;
    for (int i = 0; i < l->recent_count; i++)
        if (strcmp(l->recent[i], game_id) == 0) { pos = i; break; }
    if (pos == l->recent_count) l->recent_count++;
    memmove(l->recent[1], l->recent[0], (size_t)pos * LIB_ID_LEN);
    utf8_copy(l->recent[0], LIB_ID_LEN, game_id);

    if (disc <= 0) return;
    for (int i = 0; i < l->last_disc_count; i++)
        if (strcmp(l->last_disc[i].id, game_id) == 0) { l->last_disc[i].disc = disc; return; }
    if (l->last_disc_count == LIB_MAX_DISCS) {   // drop the oldest entry
        memmove(&l->last_disc[0], &l->last_disc[1], (LIB_MAX_DISCS - 1) * sizeof *l->last_disc);
        l->last_disc_count--;
    }
    LibDisc *d = &l->last_disc[l->last_disc_count++];
    utf8_copy(d->id, LIB_ID_LEN, game_id);
    d->disc = disc;
}

int library_last_disc(const Library *l, const char *game_id) {
    for (int i = 0; i < l->last_disc_count; i++)
        if (strcmp(l->last_disc[i].id, game_id) == 0) return l->last_disc[i].disc;
    return 0;
}

static const char *json_str(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

int library_load(Library *l, const char *path) {
    library_init(l);
    int missing = 0;
    char *text = safe_read_text(path, 4 * 1024 * 1024, &missing);
    if (!text) return missing ? 0 : -1;        // first run: nothing saved yet
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }

    const char *lang = json_str(root, "language");
    if (lang && (strcmp(lang, "ru") == 0 || strcmp(lang, "en") == 0)) strcpy(l->language, lang);
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "settings");   // absent before 1.1.7
    if (cJSON_IsObject(st)) {
        read_flag(st, "xmb_exit", &l->settings.xmb_exit);
        read_flag(st, "ncl_logo", &l->settings.ncl_logo);
        read_flag(st, "site", &l->settings.site);
        read_flag(st, "show_music", &l->settings.show_music);
        read_flag(st, "replace_xmb", &l->settings.replace_xmb);
    }

    const cJSON *folder;
    cJSON_ArrayForEach(folder, cJSON_GetObjectItemCaseSensitive(root, "folders")) {
        const char *name = json_str(folder, "name");
        int idx = name ? library_folder_create(l, name) : -1;
        if (idx < 0) continue;
        const cJSON *id;
        cJSON_ArrayForEach(id, cJSON_GetObjectItemCaseSensitive(folder, "games"))
            if (cJSON_IsString(id)) library_folder_add(l, idx, id->valuestring);
    }
    // Recent is stored most-recent-first; replay oldest first to rebuild the order.
    const cJSON *recent = cJSON_GetObjectItemCaseSensitive(root, "recent");
    for (int i = cJSON_GetArraySize(recent) - 1; i >= 0; i--) {
        const cJSON *id = cJSON_GetArrayItem(recent, i);
        if (cJSON_IsString(id)) library_touch_recent(l, id->valuestring, 0);
    }
    const cJSON *disc;
    cJSON_ArrayForEach(disc, cJSON_GetObjectItemCaseSensitive(root, "last_disc")) {
        const char *id = json_str(disc, "id");
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(disc, "disc");
        if (id && cJSON_IsNumber(n) && n->valueint > 0 && l->last_disc_count < LIB_MAX_DISCS) {
            LibDisc *d = &l->last_disc[l->last_disc_count++];
            utf8_copy(d->id, LIB_ID_LEN, id);
            d->disc = n->valueint;
        }
    }
    cJSON_Delete(root);
    return 0;
}

static void make_parent_dirs(const char *path) {
    char buf[512];
    utf8_copy(buf, sizeof buf, path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        if (p[-1] == ':') continue;            // "ms0:/" device root
        *p = '\0';
        mkdir(buf, 0777);
        *p = '/';
    }
}

int library_save(const Library *l, const char *path) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", LIB_FORMAT_VERSION);
    cJSON_AddStringToObject(root, "language", l->language);
    cJSON *st = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddBoolToObject(st, "xmb_exit", l->settings.xmb_exit);
    cJSON_AddBoolToObject(st, "ncl_logo", l->settings.ncl_logo);
    cJSON_AddBoolToObject(st, "site", l->settings.site);
    cJSON_AddBoolToObject(st, "show_music", l->settings.show_music);
    cJSON_AddBoolToObject(st, "replace_xmb", l->settings.replace_xmb);
    cJSON *folders = cJSON_AddArrayToObject(root, "folders");
    for (int i = 0; i < l->folder_count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", l->folders[i].name);
        cJSON *games = cJSON_AddArrayToObject(f, "games");
        for (int j = 0; j < l->folders[i].count; j++)
            cJSON_AddItemToArray(games, cJSON_CreateString(l->folders[i].ids[j]));
        cJSON_AddItemToArray(folders, f);
    }
    cJSON *recent = cJSON_AddArrayToObject(root, "recent");
    for (int i = 0; i < l->recent_count; i++)
        cJSON_AddItemToArray(recent, cJSON_CreateString(l->recent[i]));
    cJSON *discs = cJSON_AddArrayToObject(root, "last_disc");
    for (int i = 0; i < l->last_disc_count; i++) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "id", l->last_disc[i].id);
        cJSON_AddNumberToObject(d, "disc", l->last_disc[i].disc);
        cJSON_AddItemToArray(discs, d);
    }
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return -1;

    make_parent_dirs(path);
    int rc = safe_write_text(path, text);
    free(text);
    return rc;
}
