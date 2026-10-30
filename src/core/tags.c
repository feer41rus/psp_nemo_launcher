#include "tags.h"
#include "utf8.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TEXT_FRAME_MAX 4096   // longer text frames are skipped
#define OGG_SCAN 65536

AudioFormat audio_format_from_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return AF_UNKNOWN;
    if (strcasecmp(dot, ".mp3") == 0) return AF_MP3;
    if (strcasecmp(dot, ".flac") == 0) return AF_FLAC;
    if (strcasecmp(dot, ".ogg") == 0) return AF_OGG;
    if (strcasecmp(dot, ".wav") == 0) return AF_WAV;
    return AF_UNKNOWN;
}

// ---------- text ----------

static int put_utf8(char *out, size_t cap, size_t *o, uint32_t cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    if (*o + (size_t)n >= cap) return 0;   // never split a character
    memcpy(out + *o, b, (size_t)n);
    *o += (size_t)n;
    return 1;
}

static int is_cyr_byte(uint8_t c) { return c >= 0xC0 || c == 0xA8 || c == 0xB8; }

// cp1251 when Cyrillic-range letters form a run of 3+ or more than half of all letters.
// Latin-1 accents sit alone or in pairs inside Latin words ("Canção", "Größe", "Motörhead").
int cp1251_likely(const uint8_t *s, size_t n) {
    int letters = 0, high = 0, run = 0, best_run = 0;
    for (size_t i = 0; i < n && s[i]; i++) {
        uint8_t c = s[i];
        if (is_cyr_byte(c)) {
            letters++;
            high++;
            if (++run > best_run) best_run = run;
        } else {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters++;
            run = 0;
        }
    }
    return best_run >= 3 || (high > 0 && high * 2 > letters);
}

static const uint16_t CP1251_80_BF[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x003F, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

uint32_t cp1251_to_unicode(uint8_t c) {
    if (c < 0x80) return c;
    if (c >= 0xC0) return 0x0410 + (c - 0xC0);
    return CP1251_80_BF[c - 0x80];
}

int text_decode(const uint8_t *s, size_t n, int enc, char *out, size_t cap) {
    size_t o = 0;
    if (cap == 0) return 0;
    if (enc == 0) {
        int cyr = cp1251_likely(s, n);
        for (size_t i = 0; i < n && s[i]; i++)
            if (!put_utf8(out, cap, &o, cyr ? cp1251_to_unicode(s[i]) : s[i])) break;
    } else if (enc == 1 || enc == 2) {
        int be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE) { be = 0; i = 2; }
            else if (s[0] == 0xFE && s[1] == 0xFF) { be = 1; i = 2; }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t u = be ? (uint32_t)(s[i] << 8 | s[i + 1]) : (uint32_t)(s[i] | s[i + 1] << 8);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {   // surrogate pair
                uint32_t lo = be ? (uint32_t)(s[i + 2] << 8 | s[i + 3]) : (uint32_t)(s[i + 2] | s[i + 3] << 8);
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            if (!put_utf8(out, cap, &o, u)) break;
        }
    } else {
        size_t len = 0;
        while (len < n && s[len]) len++;
        char tmp[512];
        if (len >= sizeof tmp) len = sizeof tmp - 1;
        memcpy(tmp, s, len);
        tmp[len] = '\0';
        o = utf8_copy(out, cap, tmp);
    }
    out[o] = '\0';
    // Trim trailing spaces (ID3v1 padding).
    while (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
    return (int)o;
}

static int leading_int(const char *s) {
    while (*s == ' ') s++;
    return atoi(s);
}

// ---------- ID3v2 ----------

static uint32_t syncsafe(const uint8_t *b) { return (uint32_t)b[0] << 21 | b[1] << 14 | b[2] << 7 | b[3]; }
static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]; }
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

static void id3_text_frame(TagInfo *t, const char *id, const uint8_t *body, uint32_t n) {
    if (n < 2) return;
    char buf[256];
    text_decode(body + 1, n - 1, body[0], buf, sizeof buf);
    if (!buf[0]) return;
    if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) utf8_copy(t->title, sizeof t->title, buf);
    else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) utf8_copy(t->artist, sizeof t->artist, buf);
    else if (!strcmp(id, "TPE2") || !strcmp(id, "TP2")) utf8_copy(t->album_artist, sizeof t->album_artist, buf);
    else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) utf8_copy(t->album, sizeof t->album, buf);
    else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) t->track_no = leading_int(buf);
    else if (!strcmp(id, "TPOS") || !strcmp(id, "TPA")) t->disc_no = leading_int(buf);
    else if (!strcmp(id, "TYER") || !strcmp(id, "TYE") || !strcmp(id, "TDRC")) {
        if (!t->year) t->year = leading_int(buf);
    }
}

// Picture frame: locate the image bytes. APIC: enc, mime\0, type, desc\0, data. PIC: enc, fmt[3], type, desc\0.
static void id3_picture(TagInfo *t, int v22, const uint8_t *head, uint32_t head_n, uint32_t body_off, uint32_t body_n) {
    if (t->cover_size || head_n < 4) return;
    uint32_t p = 1;
    if (v22) {
        p += 3;
    } else {
        while (p < head_n && head[p]) p++;
        p++;
    }
    p++;                                           // picture type
    int wide = head[0] == 1 || head[0] == 2;
    while (p < head_n) {                           // description, terminated by 1 or 2 NULs
        if (wide) {
            if (p + 1 < head_n && head[p] == 0 && head[p + 1] == 0) { p += 2; break; }
            p += 2;
        } else {
            if (head[p] == 0) { p++; break; }
            p++;
        }
    }
    if (p >= head_n || p >= body_n) return;
    t->cover_offset = body_off + p;
    t->cover_size = body_n - p;
}

static void read_id3v2(FILE *f, long file_size, TagInfo *t) {
    uint8_t h[10];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3) != 0) return;
    int ver = h[3];
    if (ver < 2 || ver > 4) return;
    uint32_t size = syncsafe(h + 6);
    if ((long)size + 10 > file_size) return;        // broken size: ignore the whole tag
    uint32_t pos = 10, end = 10 + size;
    if (ver >= 3 && (h[5] & 0x40)) {                // extended header
        uint8_t e[4];
        if (fread(e, 1, 4, f) != 4) return;
        uint32_t es = ver == 4 ? syncsafe(e) : be32(e) + 4;
        pos += es;
    }
    int hdr_len = ver == 2 ? 6 : 10;
    while (pos + (uint32_t)hdr_len <= end) {
        uint8_t fh[10];
        if (fseek(f, (long)pos, SEEK_SET) != 0 || fread(fh, 1, (size_t)hdr_len, f) != (size_t)hdr_len) return;
        if (fh[0] == 0) return;                     // padding
        char id[5] = { 0 };
        memcpy(id, fh, ver == 2 ? 3 : 4);
        uint32_t n = ver == 2 ? (uint32_t)(fh[3] << 16 | fh[4] << 8 | fh[5])
                   : ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
        uint32_t body = pos + (uint32_t)hdr_len;
        if (n == 0 || n > end - body) return;   // no-wrap form: n may be close to 2^32
        if (id[0] == 'T' && n <= TEXT_FRAME_MAX) {
            uint8_t buf[TEXT_FRAME_MAX];
            if (fread(buf, 1, n, f) != n) return;
            id3_text_frame(t, id, buf, n);
        } else if (!strcmp(id, "APIC") || !strcmp(id, "PIC")) {
            uint8_t head[512];
            uint32_t take = n < sizeof head ? n : (uint32_t)sizeof head;
            if (fread(head, 1, take, f) != take) return;
            id3_picture(t, ver == 2, head, take, body, n);
        }
        pos = body + n;
    }
}

static void read_id3v1(FILE *f, long file_size, TagInfo *t) {
    if (t->title[0] || file_size < 128) return;
    uint8_t v1[128];
    if (fseek(f, file_size - 128, SEEK_SET) != 0 || fread(v1, 1, 128, f) != 128 || memcmp(v1, "TAG", 3) != 0)
        return;
    text_decode(v1 + 3, 30, 0, t->title, sizeof t->title);
    text_decode(v1 + 33, 30, 0, t->artist, sizeof t->artist);
    text_decode(v1 + 63, 30, 0, t->album, sizeof t->album);
    char year[5] = { 0 };
    memcpy(year, v1 + 93, 4);
    t->year = atoi(year);
    if (v1[125] == 0 && v1[126] != 0) t->track_no = v1[126];   // ID3v1.1
}

// ---------- Vorbis comments ----------

static void vorbis_comments(const uint8_t *p, size_t n, TagInfo *t) {
    if (n < 8) return;
    uint32_t vlen = le32(p);
    if (vlen > n - 8) return;                       // no-wrap (size_t is 32-bit on the PSP)
    size_t o = 4 + vlen;
    uint32_t count = le32(p + o);
    o += 4;
    for (uint32_t i = 0; i < count && o + 4 <= n; i++) {
        uint32_t len = le32(p + o);
        o += 4;
        if (len > n - o) return;
        const char *kv = (const char *)p + o;
        const char *eq = memchr(kv, '=', len);
        if (eq) {
            size_t klen = (size_t)(eq - kv), vl = len - klen - 1;
            char val[256];
            if (vl >= sizeof val) vl = sizeof val - 1;
            memcpy(val, eq + 1, vl);
            val[vl] = '\0';
            char tmp[256];
            utf8_copy(tmp, sizeof tmp, val);
#define KEY(k) (klen == strlen(k) && strncasecmp(kv, k, klen) == 0)
            if (KEY("TITLE")) utf8_copy(t->title, sizeof t->title, tmp);
            else if (KEY("ARTIST")) utf8_copy(t->artist, sizeof t->artist, tmp);
            else if (KEY("ALBUM")) utf8_copy(t->album, sizeof t->album, tmp);
            else if (KEY("ALBUMARTIST") || KEY("ALBUM ARTIST")) utf8_copy(t->album_artist, sizeof t->album_artist, tmp);
            else if (KEY("TRACKNUMBER")) t->track_no = leading_int(tmp);
            else if (KEY("DISCNUMBER")) t->disc_no = leading_int(tmp);
            else if ((KEY("DATE") || KEY("YEAR")) && !t->year) t->year = leading_int(tmp);
#undef KEY
        }
        o += len;
    }
}

static void read_flac(FILE *f, long file_size, TagInfo *t) {
    uint8_t magic[4];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0) return;
    long pos = 4;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t h[4];
        if (fseek(f, pos, SEEK_SET) != 0 || fread(h, 1, 4, f) != 4) return;
        int last = h[0] & 0x80, type = h[0] & 0x7F;
        uint32_t len = (uint32_t)(h[1] << 16 | h[2] << 8 | h[3]);
        long body = pos + 4;
        if ((long)len > file_size - body) return;
        if (type == 4 && len <= 256 * 1024) {
            uint8_t *buf = malloc(len);
            if (buf && fread(buf, 1, len, f) == len) vorbis_comments(buf, len, t);
            free(buf);
        } else if (type == 6 && !t->cover_size && len >= 32) {
            uint8_t head[512];
            uint32_t take = len < sizeof head ? len : (uint32_t)sizeof head;
            if (fread(head, 1, take, f) == take) {
                uint32_t p = 4, ml = be32(head + p);
                if (ml <= take - 8) {
                    p += 4 + ml;
                    uint32_t dl = be32(head + p);
                    if (dl <= take - p - 4 && take - p - 4 - dl >= 20) {
                        p += 4 + dl + 16;
                        uint32_t size = be32(head + p);
                        p += 4;
                        if (size <= len - p) {
                            t->cover_offset = (uint32_t)body + p;
                            t->cover_size = size;
                        }
                    }
                }
            }
        }
        if (last) return;
        pos = body + (long)len;
    }
}

static void read_ogg(FILE *f, TagInfo *t) {
    uint8_t *buf = malloc(OGG_SCAN);
    if (!buf) return;
    size_t n = (fseek(f, 0, SEEK_SET) == 0) ? fread(buf, 1, OGG_SCAN, f) : 0;
    if (n > 4 && memcmp(buf, "OggS", 4) == 0) {
        for (size_t i = 0; i + 7 < n; i++) {
            if (buf[i] == 3 && memcmp(buf + i + 1, "vorbis", 6) == 0) {
                vorbis_comments(buf + i + 7, n - i - 7, t);
                break;
            }
        }
    }
    free(buf);
}

int tags_read(const char *path, AudioFormat fmt, TagInfo *out) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    switch (fmt) {
    case AF_MP3:
        read_id3v2(f, size, out);
        read_id3v1(f, size, out);
        break;
    case AF_FLAC: read_flac(f, size, out); break;
    case AF_OGG: read_ogg(f, out); break;
    default: break;
    }
    fclose(f);
    return 0;
}
