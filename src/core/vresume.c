#include "vresume.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_RESUME_MS 10000
#define END_MS 5000

void vresume_init(VResume *r) {
    memset(r, 0, sizeof *r);
    r->sub_size = 2;
    r->sub_bg = 1;
}

void vresume_free(VResume *r) {
    int size = r->sub_size, bg = r->sub_bg, pos = r->sub_pos;
    free(r->e);
    memset(r, 0, sizeof *r);
    r->sub_size = size;
    r->sub_bg = bg;
    r->sub_pos = pos;
}

void vresume_key(char *out, size_t cap, const char *path, long long size) { snprintf(out, cap, "%s|%lld", path, size); }

static VrEntry *find(const VResume *r, const char *key) {
    for (int i = 0; i < r->count; i++)
        if (!strcmp(r->e[i].key, key)) return &r->e[i];
    return NULL;
}

const VrEntry *vresume_get(const VResume *r, const char *key) { return find(r, key); }

static VrEntry *add(VResume *r, const char *key) {
    if (!r->e) {
        r->e = calloc(VRESUME_MAX, sizeof *r->e);
        if (!r->e) return NULL;
    }
    VrEntry *e;
    if (r->count < VRESUME_MAX) {
        e = &r->e[r->count++];
    } else {   // full: the one touched longest ago makes room
        e = &r->e[0];
        for (int i = 1; i < r->count; i++)
            if (r->e[i].seq < e->seq) e = &r->e[i];
    }
    memset(e, 0, sizeof *e);
    snprintf(e->key, sizeof e->key, "%s", key);
    e->sub_track = -1;
    return e;
}

void vresume_put(VResume *r, const char *key, int64_t pos_ms, int64_t dur_ms, int sub_track, int32_t sub_offset) {
    VrEntry *e = find(r, key);
    if (!e) e = add(r, key);
    if (!e) return;
    e->done = dur_ms > 0 && pos_ms >= dur_ms - END_MS;
    e->pos_ms = e->done || pos_ms < MIN_RESUME_MS ? 0 : (int32_t)pos_ms;
    e->sub_track = sub_track;
    e->sub_offset = sub_offset;
    e->seq = ++r->seq;
}

void vresume_set_audio(VResume *r, const char *key, int track) {
    VrEntry *e = find(r, key);
    if (!e) e = add(r, key);
    if (!e) return;
    e->audio_track = track;
    e->seq = ++r->seq;
}

int vresume_ask(const VrEntry *e) { return e && !e->done && e->pos_ms >= MIN_RESUME_MS; }

int vresume_load(VResume *r, const char *json) {
    vresume_free(r);
    if (!json) return 0;
    cJSON *root = cJSON_Parse(json);
    cJSON *list = root ? cJSON_GetObjectItem(root, "videos") : NULL;
    cJSON *st = root ? cJSON_GetObjectItem(root, "subtitles") : NULL, *v;
    if ((v = cJSON_GetObjectItem(st, "size")) && cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 4) r->sub_size = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(st, "background")) && cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 3) r->sub_bg = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItem(st, "height")) && cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 8) r->sub_pos = (int)v->valuedouble;
    cJSON *it;
    cJSON_ArrayForEach(it, list) {
        cJSON *k = cJSON_GetObjectItem(it, "key");
        if (!cJSON_IsString(k)) continue;
        VrEntry *e = add(r, k->valuestring);
        if (!e) break;
        if ((v = cJSON_GetObjectItem(it, "pos")) && cJSON_IsNumber(v)) e->pos_ms = (int32_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "done")) && cJSON_IsBool(v)) e->done = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(it, "sub")) && cJSON_IsNumber(v)) e->sub_track = (int)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "shift")) && cJSON_IsNumber(v)) e->sub_offset = (int32_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "seq")) && cJSON_IsNumber(v)) e->seq = (uint32_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(it, "audio")) && cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble < 16)
            e->audio_track = (int)v->valuedouble;
        if (e->seq > r->seq) r->seq = e->seq;
    }
    cJSON_Delete(root);
    return r->count;
}

char *vresume_save(const VResume *r) {
    cJSON *root = cJSON_CreateObject(), *st = cJSON_AddObjectToObject(root, "subtitles");
    cJSON_AddNumberToObject(st, "size", r->sub_size);
    cJSON_AddNumberToObject(st, "background", r->sub_bg);
    cJSON_AddNumberToObject(st, "height", r->sub_pos);
    cJSON *list = cJSON_AddArrayToObject(root, "videos");
    for (int i = 0; i < r->count; i++) {
        const VrEntry *e = &r->e[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", e->key);
        cJSON_AddNumberToObject(o, "pos", e->pos_ms);
        cJSON_AddBoolToObject(o, "done", e->done);
        cJSON_AddNumberToObject(o, "sub", e->sub_track);
        cJSON_AddNumberToObject(o, "shift", e->sub_offset);
        cJSON_AddNumberToObject(o, "seq", e->seq);
        if (e->audio_track) cJSON_AddNumberToObject(o, "audio", e->audio_track);
        cJSON_AddItemToArray(list, o);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}
