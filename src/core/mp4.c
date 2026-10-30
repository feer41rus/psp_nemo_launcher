#include "mp4.h"
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

// Walks the child boxes of [p, end): calls fn for each with its type and payload.
typedef int (*BoxFn)(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end);

static int walk(Mp4 *m, Mp4Track *t, const uint8_t *p, const uint8_t *end, BoxFn fn) {
    while (end - p >= 8) {
        uint64_t size = be32(p);
        int hdr = 8;
        if (size == 1) {
            if (end - p < 16) return -1;
            size = be64(p + 8);
            hdr = 16;
        } else if (size == 0) {
            size = (uint64_t)(end - p);
        }
        if (size < (uint64_t)hdr || size > (uint64_t)(end - p)) return -1;
        char type[5];
        memcpy(type, p + 4, 4);
        type[4] = '\0';
        if (fn(m, t, type, p + hdr, p + size) != 0) return -1;
        p += size;
    }
    return 0;
}

// A full box's table: version/flags, then a count, then entries of `entry` bytes.
static int table(const uint8_t *p, const uint8_t *end, int skip, int entry, const uint8_t **rows, uint32_t *n) {
    if (end - p < 8 + skip) return -1;
    uint32_t count = be32(p + 4 + skip);
    if ((uint64_t)count * (uint64_t)entry > (uint64_t)(end - p - 8 - skip)) return -1;
    *rows = p + 8 + skip;
    *n = count;
    return 0;
}

// esds: find the DecoderSpecificInfo (tag 5) = AudioSpecificConfig.
static void esds_config(Mp4Track *t, const uint8_t *p, const uint8_t *end) {
    p += 4;   // version/flags
    while (end - p >= 2) {
        int tag = *p++;
        uint32_t len = 0;
        for (int k = 0; k < 4 && p < end; k++) {
            uint8_t b = *p++;
            len = len << 7 | (b & 0x7f);
            if (!(b & 0x80)) break;
        }
        if (tag == 3) { p += 3; if (p <= end && p[-1] & 0x80) p += 2; continue; }   // ES_ID, flags (no URL/OCR in practice)
        if (tag == 4) { p += 13; continue; }                                        // object type, stream type, buffers, rates
        if (tag == 5) {
            if ((uint32_t)(end - p) >= len) { t->config = p; t->config_len = (int)len; }
            return;
        }
        p += len;
    }
}

static int stsd_entry(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    (void)m;
    if (!strcmp(type, "avcC")) { t->config = p; t->config_len = (int)(end - p); }
    else if (!strcmp(type, "esds")) esds_config(t, p, end);
    return 0;
}

static int parse_stsd(Mp4 *m, Mp4Track *t, const uint8_t *p, const uint8_t *end) {
    if (end - p < 16) return -1;
    const uint8_t *e = p + 8;
    uint32_t size = be32(e);
    if (size < 16 || size > (uint32_t)(end - e)) return -1;
    memcpy(t->codec, e + 4, 4);
    t->codec[4] = '\0';
    const uint8_t *body = e + 16, *eend = e + size;   // after the 8-byte SampleEntry header
    if (t->kind == MT_VIDEO && eend - body >= 70) {
        t->width = be16(body + 16);
        t->height = be16(body + 18);
        return walk(m, t, body + 70, eend, stsd_entry);
    }
    if (t->kind == MT_AUDIO && eend - body >= 20) {
        t->channels = be16(body + 8);
        t->bits = be16(body + 10);
        t->sample_rate = (int)(be32(body + 16) >> 16);
        return walk(m, t, body + 20, eend, stsd_entry);
    }
    return 0;
}

static int parse_stbl(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    const uint8_t *rows;
    uint32_t n;
    if (!strcmp(type, "stsd")) return parse_stsd(m, t, p, end);
    if (!strcmp(type, "stts")) { if (table(p, end, 0, 8, &t->stts, &t->n_stts)) return -1; }
    else if (!strcmp(type, "ctts")) { if (table(p, end, 0, 8, &t->ctts, &t->n_ctts)) return -1; }
    else if (!strcmp(type, "stsc")) { if (table(p, end, 0, 12, &t->stsc, &t->n_stsc)) return -1; }
    else if (!strcmp(type, "stss")) { if (table(p, end, 0, 4, &t->stss, &t->n_stss)) return -1; }
    else if (!strcmp(type, "stco")) { if (table(p, end, 0, 4, &t->stco, &t->n_stco)) return -1; }
    else if (!strcmp(type, "co64")) { if (table(p, end, 0, 8, &t->stco, &t->n_stco)) return -1; t->co64 = 1; }
    else if (!strcmp(type, "stsz")) {
        if (end - p < 12) return -1;
        t->fixed_size = be32(p + 4);
        n = be32(p + 8);
        if (!t->fixed_size) {
            if (table(p, end, 4, 4, &rows, &n)) return -1;
            t->stsz = rows;
        }
        t->n_samples = (int)n;
    }
    return 0;
}

static int parse_minf(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    return !strcmp(type, "stbl") ? walk(m, t, p, end, parse_stbl) : 0;
}

static int parse_mdia(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    if (!strcmp(type, "mdhd")) {
        if (end - p < 24) return -1;
        int v1 = p[0] == 1;
        if (v1 && end - p < 36) return -1;
        t->timescale = be32(p + (v1 ? 20 : 12));
        t->duration = v1 ? (int64_t)be64(p + 24) : be32(p + 16);
        uint16_t l = be16(p + (v1 ? 32 : 20));
        t->lang[0] = (char)(((l >> 10) & 31) + 0x60);
        t->lang[1] = (char)(((l >> 5) & 31) + 0x60);
        t->lang[2] = (char)((l & 31) + 0x60);
        t->lang[3] = '\0';
    } else if (!strcmp(type, "hdlr")) {
        if (end - p < 12) return -1;
        const uint8_t *h = p + 8;
        t->kind = !memcmp(h, "vide", 4) ? MT_VIDEO : !memcmp(h, "soun", 4) ? MT_AUDIO
                : (!memcmp(h, "text", 4) || !memcmp(h, "sbtl", 4)) ? MT_TEXT : MT_OTHER;
    } else if (!strcmp(type, "minf")) {
        return walk(m, t, p, end, parse_minf);
    }
    return 0;
}

static int parse_edts(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    if (strcmp(type, "elst") != 0 || end - p < 8) return 0;
    int v1 = p[0] == 1, entry = v1 ? 20 : 12;
    uint32_t n = be32(p + 4);
    const uint8_t *e = p + 8;
    for (uint32_t i = 0; i < n && end - e >= entry; i++, e += entry) {
        int64_t dur = v1 ? (int64_t)be64(e) : be32(e);
        int64_t mt = v1 ? (int64_t)be64(e + 8) : (int32_t)be32(e + 4);
        if (mt == -1) { if (m->timescale) t->empty_ms += dur * 1000 / m->timescale; continue; }
        t->media_start = mt;
        break;
    }
    return 0;
}

// First chunks from 1, rising, samples per chunk > 0 (review M4: a crafted table must not walk out).
static int stsc_valid(const Mp4Track *t) {
    uint32_t prev = 0;
    for (uint32_t i = 0; i < t->n_stsc; i++) {
        uint32_t fc = be32(t->stsc + 12 * i), spc = be32(t->stsc + 12 * i + 4);
        if (fc <= prev || spc == 0) return 0;
        prev = fc;
    }
    return 1;
}

static int parse_trak(Mp4 *m, Mp4Track *t, const char *type, const uint8_t *p, const uint8_t *end) {
    if (!strcmp(type, "mdia")) return walk(m, t, p, end, parse_mdia);
    if (!strcmp(type, "edts")) return walk(m, t, p, end, parse_edts);
    return 0;
}

static int parse_moov(Mp4 *m, Mp4Track *unused, const char *type, const uint8_t *p, const uint8_t *end) {
    (void)unused;
    if (!strcmp(type, "mvhd")) {
        if (end - p < 20) return -1;
        int v1 = p[0] == 1;
        if (v1 && end - p < 32) return -1;
        m->timescale = be32(p + (v1 ? 20 : 12));
        m->duration = v1 ? (int64_t)be64(p + 24) : be32(p + 16);
    } else if (!strcmp(type, "trak") && m->n_tracks < MP4_MAX_TRACKS) {
        Mp4Track *t = &m->tracks[m->n_tracks];
        memset(t, 0, sizeof *t);
        t->kind = MT_OTHER;
        if (walk(m, t, p, end, parse_trak) != 0) return -1;
        // QuickTime v0 PCM ('sowt', 'twos'...): stsz says 1 byte, one "sample" being one frame (review I7)
        if (t->kind == MT_AUDIO && t->fixed_size == 1 && t->bits >= 8 && t->channels > 0 && t->channels <= 8)
            t->fixed_size = (uint32_t)(t->channels * t->bits / 8);
        if (t->timescale && t->n_samples > 0 && t->stts && t->stsc && t->n_stsc >= 1 && t->stco && stsc_valid(t) &&
            (t->stsz || t->fixed_size)) m->n_tracks++;
    }
    return 0;
}

int mp4_open(Mp4 *m, Mp4ReadFn read, void *ctx, long long file_size) {
    memset(m, 0, sizeof *m);
    long long off = 0;
    uint8_t hdr[16];
    while (off + 8 <= file_size) {
        if (read(ctx, off, hdr, 16) < 8) return -1;
        uint64_t size = be32(hdr);
        int h = 8;
        if (size == 1) { size = be64(hdr + 8); h = 16; }
        else if (size == 0) size = (uint64_t)(file_size - off);
        if (size < (uint64_t)h || (long long)size > file_size - off) return -1;
        if (!memcmp(hdr + 4, "moov", 4)) {
            if (size > 32u * 1024 * 1024) return -1;
            m->moov = malloc((size_t)size);
            if (!m->moov) return -1;
            if (read(ctx, off, m->moov, (int)size) != (int)size ||
                walk(m, NULL, m->moov + h, m->moov + size, parse_moov) != 0 || !m->n_tracks) {
                mp4_close(m);
                return -1;
            }
            return 0;
        }
        off += (long long)size;
    }
    return -1;
}

void mp4_close(Mp4 *m) {
    free(m->moov);
    memset(m, 0, sizeof *m);
}

int mp4_track(const Mp4 *m, Mp4Kind kind) {
    for (int i = 0; i < m->n_tracks; i++)
        if (m->tracks[i].kind == kind) return i;
    return -1;
}

// The longest of picture and sound (a subtitle track may run past the film).
int64_t mp4_duration_ms(const Mp4 *m) {
    int64_t best = 0;
    for (int i = 0; i < m->n_tracks; i++) {
        const Mp4Track *t = &m->tracks[i];
        if (t->kind != MT_VIDEO && t->kind != MT_AUDIO) continue;
        int64_t d = (t->duration - t->media_start) * 1000 / t->timescale + t->empty_ms;
        if (d > best) best = d;
    }
    if (!best && m->timescale) best = m->duration * 1000 / m->timescale;
    return best;
}

static uint32_t sample_size(const Mp4Track *t, int i) { return t->fixed_size ? t->fixed_size : be32(t->stsz + 4 * i); }

static int is_key(const Mp4Track *t, int i) {
    if (!t->stss) return 1;
    uint32_t lo = 0, hi = t->n_stss, want = (uint32_t)i + 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, v = be32(t->stss + 4 * mid);
        if (v == want) return 1;
        if (v < want) lo = mid + 1; else hi = mid;
    }
    return 0;
}

// Decode time of sample i (timescale units) and its duration, continuing from the last lookup.
static int64_t dts_of(Mp4Track *t, int i, uint32_t *dur) {
    if (i < t->stts_first) { t->stts_run = 0; t->stts_first = 0; t->stts_dts = 0; }
    while (t->stts_run < t->n_stts) {
        uint32_t count = be32(t->stts + 8 * t->stts_run), delta = be32(t->stts + 8 * t->stts_run + 4);
        if (i < t->stts_first + (int)count) {
            *dur = delta;
            return t->stts_dts + (int64_t)(i - t->stts_first) * delta;
        }
        t->stts_first += (int)count;
        t->stts_dts += (int64_t)count * delta;
        t->stts_run++;
    }
    *dur = 0;
    return t->stts_dts;
}

static int32_t cto_of(Mp4Track *t, int i) {
    if (!t->ctts) return 0;
    if (i < t->ctts_first) { t->ctts_run = 0; t->ctts_first = 0; }
    while (t->ctts_run < t->n_ctts) {
        uint32_t count = be32(t->ctts + 8 * t->ctts_run);
        if (i < t->ctts_first + (int)count) return (int32_t)be32(t->ctts + 8 * t->ctts_run + 4);
        t->ctts_first += (int)count;
        t->ctts_run++;
    }
    return 0;
}

// File offset of sample i: its chunk from stsc, then the sizes before it in that chunk.
static long long offset_of(Mp4Track *t, int i) {
    if (i < t->stsc_first) { t->stsc_run = 0; t->stsc_first = 0; }
    for (;;) {
        uint32_t first_chunk = be32(t->stsc + 12 * t->stsc_run), spc = be32(t->stsc + 12 * t->stsc_run + 4);
        if (!spc) return -1;
        int last = t->stsc_run + 1 >= t->n_stsc;
        uint32_t next_chunk = last ? t->n_stco + 1 : be32(t->stsc + 12 * (t->stsc_run + 1));
        int run_samples = (int)((next_chunk - first_chunk) * spc);
        if (last || i < t->stsc_first + run_samples) {
            uint32_t k = (uint32_t)(i - t->stsc_first);
            uint32_t chunk = first_chunk + k / spc;   // 1-based
            if (chunk < 1 || chunk > t->n_stco) return -1;
            long long off = t->co64 ? (long long)be64(t->stco + 8 * (chunk - 1)) : be32(t->stco + 4 * (chunk - 1));
            for (int s = i - (int)(k % spc); s < i; s++) off += sample_size(t, s);
            return off;
        }
        t->stsc_first += run_samples;
        t->stsc_run++;
    }
}

static int64_t to_ms(const Mp4Track *t, int64_t v) { return v * 1000 / (int64_t)t->timescale; }

int mp4_sample(Mp4 *m, int track, int i, Mp4Sample *out) {
    if (track < 0 || track >= m->n_tracks) return -1;
    Mp4Track *t = &m->tracks[track];
    if (i < 0 || i >= t->n_samples) return -1;
    uint32_t dur;
    int64_t dts = dts_of(t, i, &dur);
    long long off = offset_of(t, i);
    if (off < 0) return -1;
    out->offset = off;
    out->size = (int)sample_size(t, i);
    out->key = is_key(t, i);
    out->dts_ms = to_ms(t, dts - t->media_start) + t->empty_ms;
    out->pts_ms = to_ms(t, dts + cto_of(t, i) - t->media_start) + t->empty_ms;
    out->dur_ms = (int)to_ms(t, dur);
    return 0;
}

int mp4_run(Mp4 *m, int track, int i, int max, long long *off, int *bytes) {
    Mp4Sample s;
    *bytes = 0;
    if (mp4_sample(m, track, i, &s) != 0) return 0;
    *off = s.offset;
    long long end = s.offset + s.size;
    int n = 1;
    *bytes = s.size;
    const Mp4Track *t = &m->tracks[track];
    if (t->fixed_size) {   // constant size: the chunk's remaining samples, without a lookup each
        uint32_t spc = be32(t->stsc + 12 * t->stsc_run + 4);
        int in_chunk = (int)(spc - (uint32_t)(i - t->stsc_first) % spc);
        n = in_chunk < max ? in_chunk : max;
        if (n > t->n_samples - i) n = t->n_samples - i;
        if ((long long)n * t->fixed_size > (1 << 26)) n = (int)((1 << 26) / t->fixed_size);   // review I6: no int overflow
        *bytes = (int)(n * t->fixed_size);
        return n;
    }
    while (n < max && mp4_sample(m, track, i + n, &s) == 0 && s.offset == end) {
        end += s.size;
        *bytes += s.size;
        n++;
    }
    return n;
}

// The sample whose decode time covers ms, from the stts runs (a few runs even in a two-hour film).
static int sample_at(const Mp4Track *t, int64_t ms) {
    int64_t target = (ms - t->empty_ms) * (int64_t)t->timescale / 1000 + t->media_start;
    if (target <= 0) return 0;
    int64_t at = 0;
    int first = 0;
    for (uint32_t r = 0; r < t->n_stts; r++) {
        uint32_t count = be32(t->stts + 8 * r), delta = be32(t->stts + 8 * r + 4);
        int64_t span = (int64_t)count * delta;
        if (delta && target < at + span) {
            int k = first + (int)((target - at) / delta);
            return k < t->n_samples ? k : t->n_samples - 1;
        }
        at += span;
        first += (int)count;
    }
    return t->n_samples > 0 ? t->n_samples - 1 : 0;
}

int mp4_key_before(const Mp4 *m, int track, int64_t ms) {
    if (track < 0 || track >= m->n_tracks) return 0;
    const Mp4Track *t = &m->tracks[track];
    int i = sample_at(t, ms);
    if (!t->stss) return i;
    uint32_t lo = 0, hi = t->n_stss, best = 1;   // the last sync sample (1-based) <= i + 1
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, v = be32(t->stss + 4 * mid);
        if (v <= (uint32_t)i + 1) { best = v; lo = mid + 1; } else hi = mid;
    }
    return (int)best - 1;
}

int mp4_sample_at(Mp4 *m, int track, int64_t ms) {
    if (track < 0 || track >= m->n_tracks) return 0;
    return sample_at(&m->tracks[track], ms);
}

void mp4_text(const void *sample, int n, char *out, int cap) {
    const uint8_t *p = sample;
    if (cap <= 0) return;
    out[0] = '\0';
    if (n < 2) return;
    int len = be16(p);
    if (len > n - 2) len = n - 2;
    if (len > cap - 1) len = cap - 1;
    memcpy(out, p + 2, (size_t)len);
    out[len] = '\0';
}
