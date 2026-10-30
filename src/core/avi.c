#include "avi.h"
#include <stdlib.h>
#include <string.h>

#define MAX_HDRL (256 * 1024)
#define MAX_CHUNKS (4 * 1024 * 1024)

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

typedef struct { int kind[8]; int n; } Streams;   // per stream number: 'v', 'a' or 0

static void parse_hdrl(Avi *a, Streams *st, const uint8_t *p, const uint8_t *end) {
    int cur_kind = 0;
    while (end - p >= 8) {
        uint32_t size = le32(p + 4);
        const uint8_t *body = p + 8;
        if (size > (uint32_t)(end - body)) return;
        if (!memcmp(p, "LIST", 4) && size >= 4) {
            parse_hdrl(a, st, body + 4, body + size);   // strl lists
        } else if (!memcmp(p, "avih", 4) && size >= 40) {
            a->us_per_frame = le32(body);
            a->width = (int)le32(body + 32);
            a->height = (int)le32(body + 36);
        } else if (!memcmp(p, "strh", 4) && size >= 8) {
            cur_kind = !memcmp(body, "vids", 4) ? 'v' : !memcmp(body, "auds", 4) ? 'a' : 0;
            if (st->n < 8) st->kind[st->n++] = cur_kind;
        } else if (!memcmp(p, "strf", 4)) {
            if (cur_kind == 'v' && size >= 20) {
                memcpy(a->vcodec, body + 16, 4);
                a->vcodec[4] = '\0';
                if (!a->width) { a->width = (int)le32(body + 4); a->height = abs((int32_t)le32(body + 8)); }
            } else if (cur_kind == 'a' && size >= 16 && !a->has_audio) {
                a->has_audio = 1;
                a->a_format = le16(body);
                a->a_channels = le16(body + 2);
                a->a_rate = (int)le32(body + 4);
                a->a_block_align = le16(body + 12);
                a->a_bits = le16(body + 14);
            }
        }
        p = body + size + (size & 1);
    }
}

static int push(AviChunk **list, int *n, int *cap, long long off, int size) {
    if (*n >= MAX_CHUNKS) return -1;
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 256;
        AviChunk *l = realloc(*list, (size_t)c * sizeof *l);
        if (!l) return -1;
        *list = l;
        *cap = c;
    }
    (*list)[*n].offset = off;
    (*list)[*n].size = size;
    (*n)++;
    return 0;
}

// "00dc" -> stream 0; then its kind.
static int chunk_kind(const Streams *st, const uint8_t *id) {
    if (id[0] < '0' || id[0] > '9' || id[1] < '0' || id[1] > '9') return 0;
    int s = (id[0] - '0') * 10 + (id[1] - '0');
    return s < st->n ? st->kind[s] : 0;
}

static int add_chunk(Avi *a, const Streams *st, const uint8_t *id, long long off, int size, int *vcap, int *acap) {
    int k = chunk_kind(st, id);
    if (k == 'v' && size > 0) return push(&a->video, &a->n_video, vcap, off, size);
    if (k == 'a' && size > 0) {
        a->audio_bytes += size;
        return push(&a->audio, &a->n_audio, acap, off, size);
    }
    return 0;
}

static int read_idx1(Avi *a, const Streams *st, AviReadFn read, void *ctx, long long pos, uint32_t size,
                     long long movi_fourcc, long long file_size) {
    uint8_t *idx = malloc(size ? size : 1);
    if (!idx || read(ctx, pos, idx, (int)size) != (int)size) { free(idx); return -1; }
    int vcap = 0, acap = 0, rc = 0;
    long long base = -1;
    for (uint32_t i = 0; i + 16 <= size && rc == 0; i += 16) {
        const uint8_t *e = idx + i;
        uint32_t off = le32(e + 8), len = le32(e + 12);
        if (base < 0) base = (long long)off < movi_fourcc ? movi_fourcc : 0;   // relative to 'movi', or absolute
        long long payload = base + off + 8;
        if (payload + len > file_size) { rc = -1; break; }
        rc = add_chunk(a, st, e, payload, (int)len, &vcap, &acap);
    }
    free(idx);
    return rc;
}

static int walk_movi(Avi *a, const Streams *st, AviReadFn read, void *ctx, long long p, long long end, int *vcap, int *acap) {
    uint8_t h[12];
    while (p + 8 <= end) {
        if (read(ctx, p, h, 12) < 8) return -1;
        uint32_t size = le32(h + 4);
        if (p + 8 + size > end) return a->n_video ? 0 : -1;   // a cut-off last chunk: keep what came before
        if (!memcmp(h, "LIST", 4)) {
            if (walk_movi(a, st, read, ctx, p + 12, p + 8 + size, vcap, acap) != 0) return -1;
        } else if (add_chunk(a, st, h, p + 8, (int)size, vcap, acap) != 0) {
            return -1;
        }
        p += 8 + size + (size & 1);
    }
    return 0;
}

int avi_open(Avi *a, AviReadFn read, void *ctx, long long file_size) {
    memset(a, 0, sizeof *a);
    uint8_t h[12];
    if (file_size < 12 || read(ctx, 0, h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "AVI ", 4)) return -1;
    Streams st = { { 0 }, 0 };
    long long p = 12, movi = -1, movi_end = 0, idx_pos = -1;
    uint32_t idx_size = 0;
    int have_hdrl = 0;
    while (p + 8 <= file_size) {
        if (read(ctx, p, h, 12) < 8) break;
        uint32_t size = le32(h + 4);
        if (p + 8 + size > file_size) {
            if (!memcmp(h, "LIST", 4) && !memcmp(h + 8, "movi", 4)) size = (uint32_t)(file_size - p - 8);   // a cut-off recording
            else break;
        }
        if (!memcmp(h, "LIST", 4) && !memcmp(h + 8, "hdrl", 4)) {
            if (size < 4 || size > MAX_HDRL) return -1;   // review I5: never a negative read
            uint8_t *hd = malloc(size);
            if (!hd || read(ctx, p + 12, hd, (int)size - 4) != (int)size - 4) { free(hd); return -1; }
            parse_hdrl(a, &st, hd, hd + size - 4);
            free(hd);
            have_hdrl = 1;
        } else if (!memcmp(h, "LIST", 4) && !memcmp(h + 8, "movi", 4)) {
            movi = p + 8;   // the 'movi' fourcc: idx1 offsets count from here
            movi_end = p + 8 + size;
        } else if (!memcmp(h, "idx1", 4)) {
            idx_pos = p + 8;
            idx_size = size;
        }
        p += 8 + size + (size & 1);
    }
    if (!have_hdrl || movi < 0 || !a->vcodec[0]) { avi_close(a); return -1; }
    int rc = -1;
    if (idx_pos >= 0) rc = read_idx1(a, &st, read, ctx, idx_pos, idx_size, movi, file_size);
    if (rc != 0 || !a->n_video) {   // no index, or a broken one: walk the chunks
        free(a->video); free(a->audio);
        a->video = a->audio = NULL;
        a->n_video = a->n_audio = 0;
        a->audio_bytes = 0;
        int vcap = 0, acap = 0;
        rc = walk_movi(a, &st, read, ctx, movi + 4, movi_end, &vcap, &acap);
    }
    if (rc != 0 || !a->n_video || !a->us_per_frame) { avi_close(a); return -1; }
    return 0;
}

void avi_close(Avi *a) {
    free(a->video);
    free(a->audio);
    memset(a, 0, sizeof *a);
}

int64_t avi_duration_ms(const Avi *a) { return (int64_t)a->n_video * a->us_per_frame / 1000; }

int avi_frame_at(const Avi *a, int64_t ms) {
    if (!a->us_per_frame || ms <= 0) return 0;
    int64_t f = ms * 1000 / a->us_per_frame;
    return f >= a->n_video ? a->n_video - 1 : (int)f;
}

long long avi_audio_at(const Avi *a, int64_t ms) {
    if (!a->has_audio || ms <= 0) return 0;
    int frame = a->a_channels * a->a_bits / 8;
    if (frame <= 0) return 0;
    long long b = (long long)ms * a->a_rate / 1000 * frame;
    return b > a->audio_bytes ? a->audio_bytes : b;
}
