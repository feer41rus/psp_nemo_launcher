#include "aviwrite.h"
#include <stdlib.h>
#include <string.h>

// Fixed header layout (offsets of the fields patched on close).
#define OFF_RIFF_SIZE 4
#define OFF_AVIH_FRAMES (12 + 12 + 8 + 16)
#define HDR_VIDEO_STRL (4 + 8 + 56 + 8 + 40)   // 'strl' + strh + strf (BITMAPINFOHEADER)
#define HDR_AUDIO_STRL (4 + 8 + 56 + 8 + 16)   // 'strl' + strh + strf (WAVEFORMATEX, PCM)

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

static int patch32(FILE *f, long at, uint32_t v) {
    uint8_t b[4];
    put32(b, v);
    return fseek(f, at, SEEK_SET) == 0 && fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static long video_strh_pos(void) { return 12 + 12 + 8 + 56 + 12 + 8; }   // RIFF, LIST hdrl, avih, LIST strl, strh header
static long audio_strh_pos(void) { return video_strh_pos() + 56 + 8 + 40 + 12 + 8; }

int aviw_open(AviWriter *w, const char *path, int width, int height, int fps, int audio_rate, int audio_channels) {
    memset(w, 0, sizeof *w);
    if (fps <= 0 || width <= 0 || height <= 0) return -1;
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    w->fps = fps;
    w->rate = audio_rate;
    w->channels = audio_channels > 0 ? audio_channels : 1;
    int sound = audio_rate > 0;
    uint32_t hdrl = 4 + 8 + 56 + 12 + HDR_VIDEO_STRL - 4 + (sound ? 12 + HDR_AUDIO_STRL - 4 : 0);
    uint8_t h[512];
    memset(h, 0, sizeof h);
    uint8_t *p = h;
    memcpy(p, "RIFF", 4); memcpy(p + 8, "AVI ", 4); p += 12;
    memcpy(p, "LIST", 4); put32(p + 4, hdrl); memcpy(p + 8, "hdrl", 4); p += 12;
    memcpy(p, "avih", 4); put32(p + 4, 56); p += 8;
    put32(p, 1000000u / (uint32_t)fps);   // microseconds per frame
    put32(p + 12, 0x10);                  // AVIF_HASINDEX
    put32(p + 24, sound ? 2 : 1);         // streams
    put32(p + 28, 1024 * 1024);           // suggested buffer
    put32(p + 32, (uint32_t)width);
    put32(p + 36, (uint32_t)height);
    p += 56;
    memcpy(p, "LIST", 4); put32(p + 4, HDR_VIDEO_STRL); memcpy(p + 8, "strl", 4); p += 12;
    memcpy(p, "strh", 4); put32(p + 4, 56); p += 8;
    memcpy(p, "vids", 4); memcpy(p + 4, "MJPG", 4);
    put32(p + 20, 1); put32(p + 24, (uint32_t)fps);   // scale, rate
    put32(p + 36, 1024 * 1024);
    put32(p + 40, 0xFFFFFFFFu);                       // quality: default
    put16(p + 52, (uint32_t)width); put16(p + 54, (uint32_t)height);
    p += 56;
    memcpy(p, "strf", 4); put32(p + 4, 40); p += 8;
    put32(p, 40); put32(p + 4, (uint32_t)width); put32(p + 8, (uint32_t)height);
    put16(p + 12, 1); put16(p + 14, 24); memcpy(p + 16, "MJPG", 4);
    put32(p + 20, (uint32_t)(width * height * 3));
    p += 40;
    if (sound) {
        uint32_t align = 2u * (uint32_t)w->channels;
        memcpy(p, "LIST", 4); put32(p + 4, HDR_AUDIO_STRL); memcpy(p + 8, "strl", 4); p += 12;
        memcpy(p, "strh", 4); put32(p + 4, 56); p += 8;
        memcpy(p, "auds", 4);
        put32(p + 20, align); put32(p + 24, (uint32_t)audio_rate * align);   // scale, rate: bytes
        put32(p + 36, 16384);
        put32(p + 40, 0xFFFFFFFFu);
        put32(p + 44, align);                                                  // sample size
        p += 56;
        memcpy(p, "strf", 4); put32(p + 4, 16); p += 8;
        put16(p, 1); put16(p + 2, (uint32_t)w->channels); put32(p + 4, (uint32_t)audio_rate);
        put32(p + 8, (uint32_t)audio_rate * align); put16(p + 12, align); put16(p + 14, 16);
        p += 16;
    }
    memcpy(p, "LIST", 4); memcpy(p + 8, "movi", 4);
    w->movi_pos = (long)(p - h) + 8;
    p += 12;
    size_t n = (size_t)(p - h);
    if (fwrite(h, 1, n, w->f) != n) { fclose(w->f); w->f = NULL; return -1; }
    w->pos = (long)n;
    return 0;
}

static int add_idx(AviWriter *w, const char *id, uint32_t offset, uint32_t size) {
    if (w->n_idx == w->cap_idx) {
        int c = w->cap_idx ? w->cap_idx * 2 : 1024;
        AviwIdx *l = realloc(w->idx, (size_t)c * sizeof *l);
        if (!l) return -1;
        w->idx = l;
        w->cap_idx = c;
    }
    AviwIdx *e = &w->idx[w->n_idx++];
    memcpy(e->id, id, 4);
    e->offset = offset;
    e->size = size;
    return 0;
}

static int chunk(AviWriter *w, const char *id, const void *data, uint32_t size) {
    if (!w->f || w->failed) return -1;
    uint8_t h[8];
    memcpy(h, id, 4);
    put32(h + 4, size);
    static const uint8_t pad = 0;
    uint32_t off = (uint32_t)(w->pos - w->movi_pos);
    if (fwrite(h, 1, 8, w->f) != 8 || fwrite(data, 1, size, w->f) != size || ((size & 1) && fwrite(&pad, 1, 1, w->f) != 1)) {
        w->failed = 1;
        return -1;
    }
    w->pos += 8 + (long)size + (long)(size & 1);
    return add_idx(w, id, off, size);
}

int aviw_video(AviWriter *w, const void *jpeg, uint32_t size) {
    if (!jpeg || !size) {   // a frame late: the last one again (constant rate keeps the sound in step)
        if (!w->last_size) return -1;
        jpeg = w->last;
        size = w->last_size;
    } else {
        if (size > w->last_cap) {
            uint8_t *l = realloc(w->last, size);
            if (!l) return -1;
            w->last = l;
            w->last_cap = size;
        }
        memcpy(w->last, jpeg, size);
        w->last_size = size;
    }
    if (chunk(w, "00dc", jpeg, size) != 0) return -1;
    w->n_video++;
    if (size > w->max_frame) w->max_frame = size;
    return 0;
}

int aviw_audio(AviWriter *w, const void *pcm, uint32_t bytes) {
    if (w->rate <= 0 || !bytes) return -1;
    if (chunk(w, "01wb", pcm, bytes) != 0) return -1;
    w->audio_bytes += bytes;
    return 0;
}

long aviw_size(const AviWriter *w) { return w->pos + 8 + 16L * w->n_idx; }

int aviw_close(AviWriter *w) {
    if (!w->f) return -1;
    int rc = w->failed ? -1 : 0;
    uint8_t h[8];
    memcpy(h, "idx1", 4);
    put32(h + 4, (uint32_t)w->n_idx * 16);
    if (fwrite(h, 1, 8, w->f) != 8) rc = -1;
    for (int i = 0; i < w->n_idx && rc == 0; i++) {
        uint8_t e[16];
        memcpy(e, w->idx[i].id, 4);
        put32(e + 4, w->idx[i].id[2] == 'd' ? 0x10 : 0);   // every JPEG frame is a key frame
        put32(e + 8, w->idx[i].offset);
        put32(e + 12, w->idx[i].size);
        if (fwrite(e, 1, 16, w->f) != 16) rc = -1;
    }
    long end = w->pos + 8 + 16L * w->n_idx;
    uint32_t align = 2u * (uint32_t)w->channels;
    if (rc == 0) rc = patch32(w->f, OFF_RIFF_SIZE, (uint32_t)(end - 8));
    if (rc == 0) rc = patch32(w->f, OFF_AVIH_FRAMES, w->n_video);
    if (rc == 0) rc = patch32(w->f, 12 + 12 + 8 + 28, w->max_frame + 8);   // suggested buffer
    if (rc == 0) rc = patch32(w->f, video_strh_pos() + 32, w->n_video);   // length
    if (rc == 0 && w->rate > 0) rc = patch32(w->f, audio_strh_pos() + 32, w->audio_bytes / align);
    if (rc == 0) rc = patch32(w->f, w->movi_pos - 4, (uint32_t)(w->pos - w->movi_pos));   // LIST movi size
    if (fclose(w->f) != 0) rc = -1;
    w->f = NULL;
    free(w->idx);
    free(w->last);
    w->idx = NULL;
    w->last = NULL;
    return rc;
}
