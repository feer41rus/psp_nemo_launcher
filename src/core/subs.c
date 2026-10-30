#include "subs.h"
#include "tags.h"
#include "utf8.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---------- encoding ----------

static int valid_utf8(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        size_t k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        if (k == 9 || i + k >= n + (k == 0)) return 0;
        for (size_t j = 1; j <= k; j++)
            if (i + j >= n || (s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

static uint32_t cp866_to_unicode(uint8_t c) {
    if (c < 0x80) return c;
    if (c <= 0xAF) return 0x0410 + (c - 0x80);   // А..п
    if (c >= 0xE0 && c <= 0xEF) return 0x0440 + (c - 0xE0);   // р..я
    if (c == 0xF0) return 0x0401;
    if (c == 0xF1) return 0x0451;
    return '?';   // box drawing and the rest
}

static size_t put_utf8(char *o, uint32_t cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | cp >> 12);
    o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

// The whole file as UTF-8 (BOM dropped). Not valid UTF-8: cp866 when its letter range dominates, else cp1251.
static char *to_utf8(const char *data, size_t n) {
    const unsigned char *s = (const unsigned char *)data;
    if (n >= 3 && !memcmp(s, "\xEF\xBB\xBF", 3)) { s += 3; n -= 3; }
    char *out = malloc(n * 3 + 1);
    if (!out) return NULL;
    if (valid_utf8(s, n)) {
        memcpy(out, s, n);
        out[n] = '\0';
        return out;
    }
    size_t n866 = 0, n1251 = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] >= 0x80 && s[i] <= 0xAF) n866++;
        if (s[i] >= 0xC0) n1251++;
    }
    int is866 = n866 > n1251;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) o += put_utf8(out + o, is866 ? cp866_to_unicode(s[i]) : cp1251_to_unicode(s[i]));
    out[o] = '\0';
    return out;
}

// ---------- the list ----------

void subs_free(SubList *l) {
    for (int i = 0; i < l->count; i++) free(l->cues[i].text);
    free(l->cues);
    memset(l, 0, sizeof *l);
}

// Tags out: <i>, {\an8}; ASS breaks \N \n become new lines, \h a space; spaces trimmed per line.
static void clean(char *t) {
    char *w = t;
    for (char *r = t; *r;) {
        if (*r == '<') { char *e = strchr(r, '>'); if (e) { r = e + 1; continue; } }
        if (*r == '{') { char *e = strchr(r, '}'); if (e) { r = e + 1; continue; } }
        if (r[0] == '\\' && (r[1] == 'N' || r[1] == 'n')) { *w++ = '\n'; r += 2; continue; }
        if (r[0] == '\\' && r[1] == 'h') { *w++ = ' '; r += 2; continue; }
        if (*r == '\r') { r++; continue; }
        *w++ = *r++;
    }
    *w = '\0';
    // trim spaces around lines and drop empty lines
    char *src = t, *dst = t;
    while (*src) {
        char *eol = strchr(src, '\n');
        size_t len = eol ? (size_t)(eol - src) : strlen(src);
        while (len && (*src == ' ' || *src == '\t')) { src++; len--; }
        while (len && (src[len - 1] == ' ' || src[len - 1] == '\t')) len--;
        if (len) {
            if (dst != t) *dst++ = '\n';
            memmove(dst, src, len);
            dst += len;
        }
        if (!eol) break;
        src = eol + 1;
    }
    *dst = '\0';
}

int subs_add(SubList *l, int32_t start_ms, int32_t end_ms, const char *utf8) {
    if (start_ms < 0 || end_ms <= start_ms) return -1;
    char *t = strdup(utf8);
    if (!t) return -1;
    clean(t);
    if (!*t) { free(t); return -1; }
    if (l->count == l->cap) {
        int c = l->cap ? l->cap * 2 : 128;
        SubCue *n = realloc(l->cues, (size_t)c * sizeof *n);
        if (!n) { free(t); return -1; }
        l->cues = n;
        l->cap = c;
    }
    l->cues[l->count++] = (SubCue){ start_ms, end_ms, t };
    return 0;
}

static int by_start(const void *a, const void *b) {
    const SubCue *x = a, *y = b;
    return x->start_ms != y->start_ms ? (x->start_ms < y->start_ms ? -1 : 1) : (x->end_ms < y->end_ms ? -1 : x->end_ms > y->end_ms);
}

void subs_sort(SubList *l) {
    if (l->count > 1) qsort(l->cues, (size_t)l->count, sizeof *l->cues, by_start);
}

// ---------- .srt ----------

// "HH:MM:SS,mmm" (or '.'); -1 if not a time. *end -> after it.
static int32_t srt_time(const char *s, const char **end) {
    int h, m, sec, ms, k = 0;
    if (sscanf(s, " %d:%d:%d%*[,.]%d%n", &h, &m, &sec, &ms, &k) != 4 || k == 0) return -1;
    if (m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0 || ms > 999 || h < 0) return -1;
    *end = s + k;
    return ((h * 60 + m) * 60 + sec) * 1000 + ms;
}

static void parse_srt(SubList *l, char *t) {
    char *line = t;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        char *arrow = strstr(line, "-->");
        if (arrow) {
            const char *e;
            int32_t a = srt_time(line, &e), b = srt_time(arrow + 3, &e);
            // the text: following lines up to an empty one
            char text[2048];
            size_t k = 0;
            while (next && *next && *next != '\r' && *next != '\n') {
                char *nl = strchr(next, '\n');
                if (nl) *nl = '\0';
                size_t len = strlen(next);
                if (len && next[len - 1] == '\r') len--;
                if (k + len + 2 < sizeof text) {
                    if (k) text[k++] = '\n';
                    memcpy(text + k, next, len);
                    k += len;
                }
                next = nl ? nl + 1 : NULL;
            }
            text[k] = '\0';
            if (a >= 0 && b >= 0) subs_add(l, a, b, text);
        }
        line = next;
    }
}

// ---------- .ass / .ssa ----------

static int32_t ass_time(const char *s) {   // H:MM:SS.cc
    int h, m, sec, cs;
    if (sscanf(s, " %d:%d:%d.%d", &h, &m, &sec, &cs) != 4) return -1;
    return ((h * 60 + m) * 60 + sec) * 1000 + cs * 10;
}

static void parse_ass(SubList *l, char *t) {
    int i_start = 1, i_end = 2, n_fields = 10;   // the usual Format
    for (char *line = t; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        if (!strncasecmp(line, "Format:", 7)) {
            int f = 0;
            for (char *p = line + 7; p;) {
                while (*p == ' ') p++;
                if (!strncasecmp(p, "Start", 5)) i_start = f;
                if (!strncasecmp(p, "End", 3)) i_end = f;
                f++;
                p = strchr(p, ',');
                if (p) p++;
            }
            n_fields = f;
        } else if (!strncasecmp(line, "Dialogue:", 9)) {
            char *fields[16] = { 0 };
            char *p = line + 9;
            int f = 0;
            for (; f < n_fields - 1 && f < 15 && p; f++) {   // the text is the last field: its commas stay
                fields[f] = p;
                p = strchr(p, ',');
                if (p) *p++ = '\0';
            }
            if (p && f == n_fields - 1 && i_start < f && i_end < f && fields[i_start] && fields[i_end]) {   // review M5
                int32_t a = ass_time(fields[i_start]), b = ass_time(fields[i_end]);
                if (a >= 0 && b >= 0) subs_add(l, a, b, p);
            }
        }
        line = next;
    }
}

int subs_parse(SubList *l, const char *data, size_t n) {
    subs_free(l);
    char *t = to_utf8(data, n);
    if (!t) return 0;
    if (strstr(t, "[Events]") || strstr(t, "Dialogue:")) parse_ass(l, t);
    else parse_srt(l, t);
    free(t);
    subs_sort(l);
    return l->count;
}

const char *subs_at(const SubList *l, int64_t ms, int32_t offset_ms, char *out, size_t cap) {
    int64_t t = ms + offset_ms;
    size_t k = 0;
    out[0] = '\0';
    for (int i = 0; i < l->count && l->cues[i].start_ms <= t; i++) {
        const SubCue *c = &l->cues[i];
        if (t >= c->end_ms) continue;
        size_t len = strlen(c->text);
        if (k + len + 2 > cap) break;
        if (k) out[k++] = '\n';
        memcpy(out + k, c->text, len);
        k += len;
        out[k] = '\0';
    }
    return out;
}

// ---------- wrap ----------

int subs_wrap(const char *text, int max_w, int (*width)(void *ctx, const char *s), void *ctx,
              char lines[SUBS_MAX_LINES][256]) {
    int n = 0;
    char cur[256] = "";
    const char *p = text;
    while (*p && n < SUBS_MAX_LINES) {
        if (*p == '\n') {   // a forced break
            snprintf(lines[n++], 256, "%s", cur);
            cur[0] = '\0';
            p++;
            continue;
        }
        if (*p == ' ') { p++; continue; }
        size_t wl = strcspn(p, " \n");
        char word[256];
        if (wl > sizeof word - 1) wl = sizeof word - 1;
        memcpy(word, p, wl);
        word[wl] = '\0';
        p += wl;
        char tryl[512];
        snprintf(tryl, sizeof tryl, "%s%s%s", cur, cur[0] ? " " : "", word);
        if (width(ctx, tryl) <= max_w && strlen(tryl) < sizeof cur) { memcpy(cur, tryl, strlen(tryl) + 1); continue; }
        if (cur[0]) {   // the word goes to the next line
            snprintf(lines[n++], 256, "%s", cur);
            cur[0] = '\0';
            if (n >= SUBS_MAX_LINES) break;
        }
        // a word wider than a line is broken by letters
        const char *w = word;
        while (*w && n < SUBS_MAX_LINES) {
            size_t k = 0;
            char part[256] = "";
            while (w[k]) {
                size_t step = utf8_next(w + k);
                char t2[256];
                memcpy(t2, w, k + step);
                t2[k + step] = '\0';
                if (width(ctx, t2) > max_w && k) break;
                k += step;
                memcpy(part, w, k);
                part[k] = '\0';
            }
            w += k;
            if (*w) snprintf(lines[n++], 256, "%s", part);
            else snprintf(cur, sizeof cur, "%s", part);
        }
    }
    if (cur[0] && n < SUBS_MAX_LINES) snprintf(lines[n++], 256, "%s", cur);
    return n;
}

// ---------- files next to a video ----------

static int sub_ext(const char *name) {
    size_t n = strlen(name);
    return n > 4 && (!strcasecmp(name + n - 4, ".srt") || !strcasecmp(name + n - 4, ".ass") || !strcasecmp(name + n - 4, ".ssa"));
}

typedef struct { char name[256]; int exact; } Found;

static int by_found(const void *a, const void *b) {
    const Found *x = a, *y = b;
    if (x->exact != y->exact) return y->exact - x->exact;
    return strcasecmp(x->name, y->name);
}

int subs_find(const char *dir, const char *video_name, char (*names)[256], int max) {
    char stem[256];
    snprintf(stem, sizeof stem, "%s", video_name);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
    size_t sl = strlen(stem);
    DIR *d = opendir(dir);
    if (!d) return 0;
    Found found[32];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 32) {
        const char *nm = e->d_name;
        if (!sub_ext(nm) || strncasecmp(nm, stem, sl) != 0) continue;
        int exact = strlen(nm) == sl + 4;
        if (!exact && nm[sl] != '.') continue;   // "film2.srt" is not film's
        snprintf(found[n].name, sizeof found[n].name, "%s", nm);
        found[n].exact = exact;
        n++;
    }
    closedir(d);
    qsort(found, (size_t)n, sizeof found[0], by_found);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) memcpy(names[i], found[i].name, 256);
    return n;
}
