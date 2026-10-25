#include "art.h"
#include "blockdev.h"
#include "gfx.h"
#include "iso9660.h"
#include "log.h"
#include "pbp.h"
#include "music.h"
#include "platform.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_ICONS 48
#define MAX_BACKGROUNDS 3
#define MAX_COVERS 24
#define COVER_PX 160               // covers are scaled down to this before caching
#define COVER_MAX_BYTES (3 * 1024 * 1024)
#define COVER_MAX_PX 1500           // larger covers are skipped (decode memory)
#define READ_CHUNK (64 * 1024)

typedef enum { SLOT_EMPTY, SLOT_PENDING, SLOT_LOADING, SLOT_LOADED, SLOT_MISSING } SlotState;

typedef struct {
    int game;
    SlotState state;
    SDL_Texture *tex;
    uint32_t used;
} Slot;

static Slot icons[MAX_ICONS], backgrounds[MAX_BACKGROUNDS], covers[MAX_COVERS];

typedef enum { KIND_ICON, KIND_PIC1, KIND_COVER } ArtKind;
static uint32_t tick;

// One job at a time: the worker reads + decodes, the main thread creates the texture.
typedef enum { JOB_IDLE, JOB_QUEUED, JOB_DONE } JobState;
static struct {
    volatile JobState state;
    Game game;            // copy: the worker must not touch the shared list
    ArtKind kind;
    char cover_folder[256];       // album folder (folder covers)
    char cover_track[256];        // track with an embedded picture, "" if none
    uint32_t cover_offset, cover_size;
    Slot *slot;
    uint32_t generation;
    SDL_Surface *result;
    uint32_t ms;
    int from_cache;
} job;
static SDL_sem *job_ready;
static SDL_Thread *worker;
static uint32_t generation;   // bumped by art_reset so stale results are dropped

static uint8_t *load_png(const Game *g, int background, size_t *size) {
    if (g->format == GF_ISO) {
        BlockDev *d = blockdev_open(g->path);
        if (!d) return NULL;
        uint8_t *data = iso_read_file(d, background ? "PSP_GAME/PIC1.PNG" : "PSP_GAME/ICON0.PNG", size);
        blockdev_close(d);
        return data;
    }
    FILE *f = fopen(g->path, "rb");
    if (!f) return NULL;
    PbpHeader h;
    uint8_t *data = NULL;
    if (pbp_read_header(f, &h) == 0) data = pbp_read_section(f, &h, background ? PBP_PIC1 : PBP_ICON0, size);
    fclose(f);
    return data;
}

// Disk cache: decoded pixels, so the next load skips ISO lookup and PNG decoding.
// File: "NRAW", u16 w, u16 h, u32 SDL pixel format, rows of w * bpp bytes. w == 0 means "no image".
#define CACHE_DIR "ms0:/PSP/NEMO/cache"

static void cache_path(const char *src, char prefix, char *out, size_t cap) {
    struct stat st;
    long long size = 0, mtime = 0;
    if (stat(src, &st) == 0) { size = (long long)st.st_size; mtime = (long long)st.st_mtime; }
    uint32_t h = 2166136261u;
    for (const char *p = src; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    snprintf(out, cap, CACHE_DIR "/%c%08x_%llx_%llx.raw", prefix, (unsigned)h,
             (unsigned long long)size, (unsigned long long)mtime);
}

// 1 = hit (surface may be NULL for a cached "no image"), 0 = miss.
static int cache_read(const char *path, SDL_Surface **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t hdr[12];
    int hit = 0;
    *out = NULL;
    if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr, "NRAW", 4) == 0) {
        int w = hdr[4] | hdr[5] << 8, h = hdr[6] | hdr[7] << 8;
        uint32_t fmt = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
        if (w == 0) {
            hit = 1;
        } else if (w >= 16 && h >= 16 && w <= 512 && h <= 512) {
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, SDL_BITSPERPIXEL(fmt), fmt);
            int row = w * SDL_BYTESPERPIXEL(fmt), ok = s != NULL;
            for (int y = 0; ok && y < h; y++)
                ok = fread((uint8_t *)s->pixels + y * s->pitch, 1, (size_t)row, f) == (size_t)row;
            if (ok) { *out = s; hit = 1; }
            else if (s) SDL_FreeSurface(s);
        }
    }
    fclose(f);
    return hit;
}

static void cache_write(const char *path, SDL_Surface *s) {
    char tmp[320];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return;
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    int w = s ? s->w : 0, h = s ? s->h : 0;
    uint32_t fmt = s ? s->format->format : 0;
    uint8_t hdr[12] = { 'N', 'R', 'A', 'W', (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8),
                        (uint8_t)fmt, (uint8_t)(fmt >> 8), (uint8_t)(fmt >> 16), (uint8_t)(fmt >> 24) };
    int ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr;
    int row = s ? w * s->format->BytesPerPixel : 0;
    for (int y = 0; ok && y < h; y++)
        ok = fwrite((uint8_t *)s->pixels + y * s->pitch, 1, (size_t)row, f) == (size_t)row;
    if (fclose(f) != 0) ok = 0;
    if (ok) {
        remove(path);   // PSP rename does not overwrite (e.g. replacing a corrupt entry)
        if (rename(tmp, path) != 0) remove(tmp);
    } else {
        remove(tmp);
    }
}

static SDL_Surface *decode(const Game *g, int background) {
    size_t size = 0;
    uint8_t *png = load_png(g, background, &size);
    if (!png) return NULL;
    SDL_Surface *s = NULL;
    SDL_RWops *rw = SDL_RWFromConstMem(png, (int)size);
    if (rw) s = IMG_Load_RW(rw, 1);
    free(png);
    if (!s) return NULL;
    // Odd artwork (tiny or beyond the GPU's 512 px texture limit) is treated as "no image".
    if (s->w < 16 || s->h < 16 || s->w > 512 || s->h > 512) {
        SDL_FreeSurface(s);
        return NULL;
    }
    // Icons keep alpha; backgrounds are opaque photos, 16-bit halves memory and file size.
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, background ? SDL_PIXELFORMAT_BGR565 : SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    return conv;
}

static uint8_t *read_range(const char *path, uint32_t offset, uint32_t size) {
    if (size == 0 || size > COVER_MAX_BYTES) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = malloc(size);
    // Read in chunks and yield between them so the music stream is not stuck behind one big read.
    int ok = buf && fseek(f, (long)offset, SEEK_SET) == 0;
    for (uint32_t done = 0; ok && done < size;) {
        uint32_t n = size - done < READ_CHUNK ? size - done : READ_CHUNK;
        ok = fread(buf + done, 1, n, f) == n;
        done += n;
        SDL_Delay(2);
    }
    if (!ok) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static SDL_Surface *decode_cover_bytes(uint8_t *data, size_t size) {
    SDL_Surface *s = NULL;
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (rw) s = IMG_Load_RW(rw, 1);
    free(data);
    if (!s) return NULL;
    if (s->w < 16 || s->h < 16 || s->w > COVER_MAX_PX || s->h > COVER_MAX_PX) {
        SDL_FreeSurface(s);
        return NULL;
    }
    // Square-crop the centre and scale to COVER_PX (covers are opaque: 16-bit is enough).
    int side = s->w < s->h ? s->w : s->h;
    SDL_Rect src = { (s->w - side) / 2, (s->h - side) / 2, side, side };
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    if (!rgb) return NULL;
    SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, COVER_PX, COVER_PX, 32, SDL_PIXELFORMAT_ABGR8888);
    if (out) {
        SDL_SetSurfaceBlendMode(rgb, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(rgb, &src, out, NULL);
    }
    SDL_FreeSurface(rgb);
    if (!out) return NULL;
    SDL_Surface *small = SDL_ConvertSurfaceFormat(out, SDL_PIXELFORMAT_BGR565, 0);
    SDL_FreeSurface(out);
    return small;
}

// Embedded picture first, then cover/folder/front image in the album folder.
static SDL_Surface *decode_cover(void) {
    if (job.cover_track[0]) {
        uint8_t *b = read_range(job.cover_track, job.cover_offset, job.cover_size);
        if (b) {
            SDL_Surface *s = decode_cover_bytes(b, job.cover_size);
            if (s) return s;
        }
    }
    char path[512];
    if (music_folder_cover(job.cover_folder, path, sizeof path) == 0) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            uint8_t *b = read_range(path, 0, (uint32_t)st.st_size);
            if (b) return decode_cover_bytes(b, (size_t)st.st_size);
        }
    }
    return NULL;
}

static int worker_main(void *unused) {
    (void)unused;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    mkdir(CACHE_DIR, 0777);
    for (;;) {
        SDL_SemWait(job_ready);
        uint32_t t0 = platform_ms();
        char path[300];
        SDL_Surface *s = NULL;
        if (job.kind == KIND_COVER) {
            // Key: the album folder (a folder cover change is picked up after a rescan/restart).
            cache_path(job.cover_track[0] ? job.cover_track : job.cover_folder, 'c', path, sizeof path);
            job.from_cache = cache_read(path, &s);
            if (!job.from_cache) {
                s = decode_cover();
                cache_write(path, s);
            }
        } else {
            cache_path(job.game.path, job.kind == KIND_PIC1 ? 'b' : 'i', path, sizeof path);
            job.from_cache = cache_read(path, &s);
            if (!job.from_cache) {
                s = decode(&job.game, job.kind == KIND_PIC1);
                cache_write(path, s);
            }
        }
        job.result = s;
        job.ms = platform_ms() - t0;
        job.state = JOB_DONE;
    }
    return 0;
}

static void ensure_worker(void) {
    if (worker) return;
    job_ready = SDL_CreateSemaphore(0);
    worker = SDL_CreateThread(worker_main, "nemo_art", NULL);
    if (!worker) log_msg("art: worker thread failed: %s", SDL_GetError());
}

static void clear(Slot *slots, int n) {
    for (int i = 0; i < n; i++) {
        if (slots[i].tex) SDL_DestroyTexture(slots[i].tex);
        memset(&slots[i], 0, sizeof slots[i]);
    }
}

void art_reset(void) {
    generation++;
    clear(icons, MAX_ICONS);
    clear(backgrounds, MAX_BACKGROUNDS);
    clear(covers, MAX_COVERS);
}

static SDL_Texture *request(Slot *slots, int n, int game) {
    if (game < 0) return NULL;
    for (int i = 0; i < n; i++) {
        Slot *s = &slots[i];
        if (s->state != SLOT_EMPTY && s->game == game) {
            s->used = tick;
            return s->state == SLOT_LOADED ? s->tex : NULL;
        }
    }
    // Free slot first, otherwise the least recently used one not shown this frame and not loading.
    int victim = -1;
    for (int i = 0; i < n && victim < 0; i++)
        if (slots[i].state == SLOT_EMPTY) victim = i;
    if (victim < 0) {
        for (int i = 0; i < n; i++) {
            if (slots[i].used == tick || slots[i].state == SLOT_LOADING) continue;
            if (victim < 0 || slots[i].used < slots[victim].used) victim = i;
        }
    }
    if (victim < 0) return NULL;
    Slot *s = &slots[victim];
    if (s->tex) SDL_DestroyTexture(s->tex);
    s->tex = NULL;
    s->game = game;
    s->state = SLOT_PENDING;
    s->used = tick;
    return NULL;
}

SDL_Texture *art_icon(int game) { return request(icons, MAX_ICONS, game); }
SDL_Texture *art_background(int game) { return request(backgrounds, MAX_BACKGROUNDS, game); }
SDL_Texture *art_cover(int album) { return request(covers, MAX_COVERS, album); }

// Most recently requested pending slot: what the user looks at right now.
static Slot *next_pending(Slot *slots, int n) {
    Slot *best = NULL;
    for (int i = 0; i < n; i++)
        if (slots[i].state == SLOT_PENDING && (!best || slots[i].used > best->used)) best = &slots[i];
    return best;
}

void art_tick(const GameList *games, const MusicLib *music) {
    tick++;
    ensure_worker();
    if (job.state == JOB_DONE) {
        Slot *s = job.slot;
        int fresh = job.generation == generation && s->state == SLOT_LOADING;
        if (fresh) {
            s->tex = gfx_texture_from_surface(job.result);
            if (s->tex && job.kind == KIND_COVER) SDL_SetTextureScaleMode(s->tex, SDL_ScaleModeLinear);
            s->state = s->tex ? SLOT_LOADED : SLOT_MISSING;
            static const char *const KIND_NAMES[] = { "icon0", "pic1", "cover" };
            log_msg("art: %s #%d %s in %u ms%s", KIND_NAMES[job.kind], s->game,
                    s->tex ? "ok" : "missing", (unsigned)job.ms, job.from_cache ? " (cache)" : "");
        }
        if (job.result) SDL_FreeSurface(job.result);
        job.result = NULL;
        job.state = JOB_IDLE;
    }
    if (job.state != JOB_IDLE || !worker) return;
    ArtKind kind = KIND_PIC1;
    Slot *s = next_pending(backgrounds, MAX_BACKGROUNDS);   // backgrounds first: they are focused
    if (!s) { s = next_pending(covers, MAX_COVERS); kind = KIND_COVER; }
    if (!s) { s = next_pending(icons, MAX_ICONS); kind = KIND_ICON; }
    if (!s) return;
    if (kind == KIND_COVER) {
        if (!music || s->game >= music->album_count) { s->state = SLOT_MISSING; return; }
        const Album *al = &music->albums[s->game];
        strcpy(job.cover_folder, al->folder);
        job.cover_track[0] = '\0';
        for (int i = 0; i < al->count; i++) {
            const Track *t = &music->tracks[al->first + i];
            if (t->cover_size) {
                strcpy(job.cover_track, t->path);
                job.cover_offset = t->cover_offset;
                job.cover_size = t->cover_size;
                break;
            }
        }
    } else {
        if (s->game >= games->count) { s->state = SLOT_MISSING; return; }
        job.game = games->items[s->game];
    }
    s->state = SLOT_LOADING;
    job.kind = kind;
    job.slot = s;
    job.generation = generation;
    job.state = JOB_QUEUED;
    SDL_SemPost(job_ready);
}
