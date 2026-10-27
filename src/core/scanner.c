#include "scanner.h"
#include "blockdev.h"
#include "cJSON.h"
#include "iso9660.h"
#include "pbp.h"
#include "safefile.h"
#include "sfo.h"
#include "utf8.h"
#include "sjis.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define ISO_MAX_DEPTH 3
#define SCAN_CACHE_VERSION 2   // 2: titles from Shift-JIS file names converted (1.2.1)

static const char *const HIDDEN_FOLDERS[] = { "NPUZ01234", "SCPS10084", "ARK_Loader" };

// One file seen by the scanner; skip = not a usable game (kept so it is not re-read).
typedef struct {
    char path[256];
    long long size, mtime;
    int skip;
    Game game;
} CacheEntry;

typedef struct {
    CacheEntry *items;
    int count, cap;
} CacheList;

typedef struct {
    GameList *out;
    CacheList old, seen;
    ScanStats *stats;
} ScanCtx;

static CacheEntry *cache_push(CacheList *l) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        CacheEntry *items = realloc(l->items, (size_t)cap * sizeof *items);
        if (!items) return NULL;
        l->items = items;
        l->cap = cap;
    }
    CacheEntry *e = &l->items[l->count++];
    memset(e, 0, sizeof *e);
    return e;
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

static void set_id(Game *g) {
    size_t i = 0;
    for (; g->path[i] && i < sizeof g->id - 1; i++) g->id[i] = (char)tolower((unsigned char)g->path[i]);
    g->id[i] = '\0';
}

// Title fallback: file or folder name without extension.
static void name_title(Game *g, const char *name) {
    sjis_to_utf8(name, g->title, sizeof g->title);   // the PSP lists names in Shift-JIS
    char *dot = strrchr(g->title, '.');
    if (dot && dot != g->title) *dot = '\0';
}

// Parse an ISO/CSO image. 0 if usable.
static int parse_iso(Game *g, const char *path, const char *name) {
    BlockDev *d = blockdev_open(path);
    if (!d) return -1;
    size_t size = 0;
    uint8_t *sfo = iso_read_file(d, "PSP_GAME/PARAM.SFO", &size);
    blockdev_close(d);
    utf8_copy(g->path, sizeof g->path, path);
    g->format = GF_ISO;
    g->kind = GK_PSP;
    if (!sfo || sfo_get_string(sfo, size, "TITLE", g->title, sizeof g->title) != 0 || !g->title[0])
        name_title(g, name);
    if (sfo) sfo_get_string(sfo, size, "DISC_ID", g->disc_id, sizeof g->disc_id);
    free(sfo);
    set_id(g);
    return 0;
}

// Parse an EBOOT.PBP. 0 if usable.
static int parse_pbp(Game *g, const char *path, const char *folder) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    PbpHeader h;
    if (pbp_read_header(f, &h) != 0) { fclose(f); return -1; }
    size_t size = 0;
    uint8_t *sfo = pbp_read_section(f, &h, PBP_PARAM_SFO, &size);
    fclose(f);

    char category[8] = "";
    if (sfo) sfo_get_string(sfo, size, "CATEGORY", category, sizeof category);
    GameFormat fmt = strcmp(category, "ME") == 0 ? GF_PBP_PS1
                   : strcmp(category, "EG") == 0 ? GF_PBP_PSN : GF_PBP_HOMEBREW;
    utf8_copy(g->path, sizeof g->path, path);
    g->format = fmt;
    g->kind = game_kind_for(fmt, folder);
    if (!sfo || sfo_get_string(sfo, size, "TITLE", g->title, sizeof g->title) != 0 || !g->title[0])
        utf8_copy(g->title, sizeof g->title, folder);
    if (sfo) sfo_get_string(sfo, size, "DISC_ID", g->disc_id, sizeof g->disc_id);
    free(sfo);
    set_id(g);
    return 0;
}

// Take a candidate file from the cache when unchanged, otherwise parse it.
static void visit(ScanCtx *ctx, const char *path, const char *name, int iso) {
    // A truncated path would launch the wrong file (or nothing): skip what does not fit.
    if (strlen(path) >= sizeof ctx->out->items[0].path) return;
    struct stat st;
    if (stat(path, &st) != 0) return;
    long long size = (long long)st.st_size, mtime = (long long)st.st_mtime;

    const CacheEntry *hit = NULL;
    for (int i = 0; i < ctx->old.count; i++) {
        const CacheEntry *e = &ctx->old.items[i];
        if (e->size == size && e->mtime == mtime && strcmp(e->path, path) == 0) { hit = e; break; }
    }
    CacheEntry *rec = cache_push(&ctx->seen);
    if (!rec) return;
    if (hit) {
        *rec = *hit;
        if (ctx->stats) ctx->stats->reused++;
    } else {
        utf8_copy(rec->path, sizeof rec->path, path);
        rec->size = size;
        rec->mtime = mtime;
        rec->skip = (iso ? parse_iso(&rec->game, path, name) : parse_pbp(&rec->game, path, name)) != 0;
        if (ctx->stats) ctx->stats->parsed++;
    }
    if (!rec->skip) {
        Game *g = gamelist_push(ctx->out);
        if (g) *g = rec->game;
    }
}

static void scan_iso_dir(ScanCtx *ctx, const char *dir, int depth) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *e;
    char path[512];
    while ((e = readdir(dp)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
        if (is_dir(path)) {
            if (depth == 0 && strcasecmp(e->d_name, "VIDEO") == 0) continue;
            if (depth < ISO_MAX_DEPTH) scan_iso_dir(ctx, path, depth + 1);
        } else if (has_ext(e->d_name, ".iso") || has_ext(e->d_name, ".cso")) {
            visit(ctx, path, e->d_name, 1);
        }
    }
    closedir(dp);
}

// Find EBOOT.PBP in a folder regardless of case; 0 if found.
static int find_eboot(const char *folder, char *out, size_t cap) {
    DIR *dp = opendir(folder);
    if (!dp) return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(dp)) != NULL) {
        if (strcasecmp(e->d_name, "EBOOT.PBP") == 0) {
            found = snprintf(out, cap, "%s/%s", folder, e->d_name) < (int)cap ? 0 : -1;
            break;
        }
    }
    closedir(dp);
    return found;
}

static int hidden_folder(const char *name) {
    if (name[0] == '.' || strchr(name, '%')) return 1;
    for (size_t i = 0; i < sizeof HIDDEN_FOLDERS / sizeof *HIDDEN_FOLDERS; i++)
        if (strcasecmp(name, HIDDEN_FOLDERS[i]) == 0) return 1;
    return 0;
}

static void scan_game_dir(ScanCtx *ctx, const char *dir) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *e;
    char folder[512], eboot[768];
    while ((e = readdir(dp)) != NULL) {
        if (hidden_folder(e->d_name)) continue;
        if (snprintf(folder, sizeof folder, "%s/%s", dir, e->d_name) >= (int)sizeof folder) continue;
        if (!is_dir(folder) || find_eboot(folder, eboot, sizeof eboot) != 0) continue;
        visit(ctx, eboot, e->d_name, 0);
    }
    closedir(dp);
}

static const char *json_str(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double json_num(const cJSON *o, const char *key, int *ok) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) { *ok = 0; return 0; }
    return v->valuedouble;
}

static void load_cache(const char *path, CacheList *out) {
    int missing;
    char *text = safe_read_text(path, 8 * 1024 * 1024, &missing);
    if (!text) return;
    cJSON *root = cJSON_Parse(text);
    free(text);
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (cJSON_IsNumber(ver) && ver->valueint == SCAN_CACHE_VERSION) {
        const cJSON *it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "entries")) {
            const char *p = json_str(it, "path"), *title = json_str(it, "title"), *id = json_str(it, "id");
            const char *disc = json_str(it, "disc_id");
            int ok = 1;
            long long size = (long long)json_num(it, "size", &ok), mtime = (long long)json_num(it, "mtime", &ok);
            int skip = (int)json_num(it, "skip", &ok);
            int game_ok = 1;
            int format = (int)json_num(it, "format", &game_ok), kind = (int)json_num(it, "kind", &game_ok);
            if (!ok || !p || (!skip && (!title || !id || !game_ok))) continue;   // malformed: re-read the file
            CacheEntry *e = cache_push(out);
            if (!e) break;
            utf8_copy(e->path, sizeof e->path, p);
            e->size = size;
            e->mtime = mtime;
            e->skip = skip;
            if (!skip) {
                utf8_copy(e->game.path, sizeof e->game.path, p);
                utf8_copy(e->game.id, sizeof e->game.id, id);
                utf8_copy(e->game.title, sizeof e->game.title, title);
                utf8_copy(e->game.disc_id, sizeof e->game.disc_id, disc ? disc : "");
                e->game.format = (GameFormat)format;
                e->game.kind = (GameKind)kind;
            }
        }
    }
    cJSON_Delete(root);
}

static void save_cache(const char *path, const CacheList *l) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", SCAN_CACHE_VERSION);
    cJSON *arr = cJSON_AddArrayToObject(root, "entries");
    for (int i = 0; i < l->count; i++) {
        const CacheEntry *e = &l->items[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "path", e->path);
        cJSON_AddNumberToObject(o, "size", (double)e->size);
        cJSON_AddNumberToObject(o, "mtime", (double)e->mtime);
        cJSON_AddNumberToObject(o, "skip", e->skip);
        if (!e->skip) {
            cJSON_AddStringToObject(o, "id", e->game.id);
            cJSON_AddStringToObject(o, "title", e->game.title);
            cJSON_AddStringToObject(o, "disc_id", e->game.disc_id);
            cJSON_AddNumberToObject(o, "format", e->game.format);
            cJSON_AddNumberToObject(o, "kind", e->game.kind);
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return;
    safe_write_text(path, text);
    free(text);
}

int scan_games_cached(const char *root, const char *cache_path, GameList *out, ScanStats *stats) {
    ScanCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.out = out;
    ctx.stats = stats;
    if (cache_path) load_cache(cache_path, &ctx.old);

    char path[512];
    snprintf(path, sizeof path, "%s/ISO", root);
    scan_iso_dir(&ctx, path, 0);
    snprintf(path, sizeof path, "%s/PSP/GAME", root);
    scan_game_dir(&ctx, path);

    if (cache_path && (!stats || stats->parsed > 0 || ctx.seen.count != ctx.old.count))
        save_cache(cache_path, &ctx.seen);
    free(ctx.old.items);
    free(ctx.seen.items);

    games_group_discs(out);
    gamelist_sort_by_title(out);
    return out->count;
}

int scan_games(const char *root, GameList *out) {
    return scan_games_cached(root, NULL, out, NULL);
}
