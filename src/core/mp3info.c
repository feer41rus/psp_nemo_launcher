#include "mp3info.h"
#include <string.h>

static const int BR_V1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
static const int BR_V2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
static const int SR[3][3] = { { 44100, 48000, 32000 }, { 22050, 24000, 16000 }, { 11025, 12000, 8000 } };

typedef struct { int mpeg, bitrate, rate, channels, spf, len; } Hdr;

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// Layer III only, no free format; len = frame length in bytes.
static int read_hdr(const uint8_t *p, Hdr *h) {
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return -1;
    int ver = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3;
    int bri = p[2] >> 4, sri = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
    if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) return -1;
    h->mpeg = ver == 3 ? 10 : ver == 2 ? 20 : 25;
    h->bitrate = (ver == 3 ? BR_V1 : BR_V2)[bri];
    h->rate = SR[ver == 3 ? 0 : ver == 2 ? 1 : 2][sri];
    h->channels = (p[3] >> 6) == 3 ? 1 : 2;
    h->spf = ver == 3 ? 1152 : 576;
    h->len = (ver == 3 ? 144 : 72) * h->bitrate * 1000 / h->rate + pad;
    return 0;
}

uint32_t mp3info_id3v2_size(const uint8_t *b, size_t len) {
    if (len < 10 || memcmp(b, "ID3", 3) != 0) return 0;
    uint32_t sz = (uint32_t)(b[6] & 0x7F) << 21 | (uint32_t)(b[7] & 0x7F) << 14 | (uint32_t)(b[8] & 0x7F) << 7 |
                  (b[9] & 0x7F);
    return 10 + sz + ((b[5] & 0x10) ? 10 : 0);
}

// Xing/Info (+ LAME) or VBRI in the first frame.
static void read_info_frame(const uint8_t *f, size_t avail, const Hdr *h, Mp3Info *m) {
    size_t xo = 4 + (h->mpeg == 10 ? (h->channels == 1 ? 17 : 32) : (h->channels == 1 ? 9 : 17));
    if (xo + 8 <= avail && (memcmp(f + xo, "Xing", 4) == 0 || memcmp(f + xo, "Info", 4) == 0)) {
        uint32_t flags = be32(f + xo + 4);
        size_t p = xo + 8;
        m->info_frame = 1;
        if ((flags & 1) && p + 4 <= avail) { m->frames = be32(f + p); p += 4; }
        if ((flags & 2) && p + 4 <= avail) { m->stream_bytes = be32(f + p); p += 4; }
        if ((flags & 4) && p + 100 <= avail) { memcpy(m->toc, f + p, 100); m->has_toc = 1; p += 100; }
        if (flags & 8) p += 4;
        if (p + 24 <= avail && memcmp(f + p, "LAME", 4) == 0) {
            const uint8_t *d = f + p + 21;
            m->enc_delay = d[0] << 4 | d[1] >> 4;
            m->enc_padding = (d[1] & 0x0F) << 8 | d[2];
        }
        return;
    }
    if (36 + 18 <= avail && memcmp(f + 36, "VBRI", 4) == 0) {
        m->info_frame = 1;
        m->stream_bytes = be32(f + 36 + 10);
        m->frames = be32(f + 36 + 14);
    }
}

int mp3info_parse(const uint8_t *head, size_t len, uint32_t audio_start, uint32_t file_size, int has_id3v1,
                  Mp3Info *out) {
    memset(out, 0, sizeof *out);
    Hdr h;
    size_t at = 0;
    for (; at + 4 <= len; at++) {
        if (read_hdr(head + at, &h) != 0) continue;
        if (at == 0) break;
        // Past padding/junk: accept a sync only when the next frame header confirms it.
        Hdr next;
        if (at + (size_t)h.len + 4 <= len && read_hdr(head + at + h.len, &next) == 0) break;
    }
    if (at + 4 > len) return -1;
    out->audio_start = audio_start + (uint32_t)at;
    out->audio_end = file_size - (has_id3v1 ? 128 : 0);
    if (out->audio_end <= out->audio_start) return -1;
    out->mpeg = h.mpeg;
    out->sample_rate = h.rate;
    out->channels = h.channels;
    out->samples_per_frame = h.spf;
    out->bitrate_kbps = h.bitrate;
    out->first_frame_len = h.len;
    read_info_frame(head + at, len - at, &h, out);
    return 0;
}

int mp3info_find_frame(const uint8_t *b, size_t len) {
    Hdr h, next;
    for (size_t at = 0; at + 4 <= len; at++)
        if (read_hdr(b + at, &h) == 0 && at + (size_t)h.len + 4 <= len && read_hdr(b + at + h.len, &next) == 0)
            return (int)at;
    return -1;
}

uint32_t mp3info_duration_ms(const Mp3Info *m) {
    if (m->frames && m->sample_rate)
        return (uint32_t)((uint64_t)m->frames * (uint32_t)m->samples_per_frame * 1000 / (uint32_t)m->sample_rate);
    if (!m->bitrate_kbps) return 0;
    return (uint32_t)((uint64_t)(m->audio_end - m->audio_start) * 8 / (uint32_t)m->bitrate_kbps);
}

uint32_t mp3info_offset_for_ms(const Mp3Info *m, uint32_t ms) {
    uint32_t dur = mp3info_duration_ms(m), size = m->audio_end - m->audio_start;
    if (!dur || !ms) return m->audio_start;
    if (ms >= dur) return m->audio_end - 1;
    uint64_t off;
    if (m->has_toc) {
        uint32_t bytes = m->stream_bytes ? m->stream_bytes : size;
        double pct = (double)ms * 100.0 / dur;
        int i = (int)pct;
        double a = m->toc[i], b = i < 99 ? m->toc[i + 1] : 256.0;
        off = (uint64_t)((a + (b - a) * (pct - i)) * bytes / 256.0);
    } else {
        off = (uint64_t)size * ms / dur;
    }
    if (off >= size) off = size - 1;
    return m->audio_start + (uint32_t)off;
}
