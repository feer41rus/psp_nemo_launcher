#include "m3u.h"
#include "tags.h"
#include "utf8.h"
#include "sjis.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_utf8(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned char c = s[i];
        size_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!k || i + k > n) return 0;
        for (size_t j = 1; j < k; j++)
            if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k;
    }
    return 1;
}

static void lower_ascii(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// Resolve "." and ".." in a '/'-separated path in place; the first component (e.g. "ms0:") is kept.
static void normalize(char *p) {
    char out[1024];
    size_t ol = 0;
    const char *s = p;
    while (*s) {
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n == 1 && s[0] == '.') {
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (ol > 0 && out[ol - 1] != '/') ol--;
            if (ol > 0) ol--;
        } else if (n > 0 || ol == 0) {
            if (ol && ol < sizeof out - 1) out[ol++] = '/';
            if (ol + n >= sizeof out) n = sizeof out - 1 - ol;
            memcpy(out + ol, s, n);
            ol += n;
        }
        s += n;
        if (*s == '/') s++;
    }
    out[ol] = 0;
    strcpy(p, out);
}

// Library track whose id ends with "/<last folder>/<file>" of path (both lowercase), -1 if none.
static int find_by_tail(const MusicLib *lib, const char *lower_path) {
    const char *file = strrchr(lower_path, '/');
    if (!file) return -1;
    const char *tail = file;
    while (tail > lower_path && tail[-1] != '/') tail--;
    if (tail > lower_path) tail--;   // include the '/' before the folder
    size_t tl = strlen(tail);
    for (int i = 0; i < lib->track_count; i++) {
        size_t il = strlen(lib->tracks[i].id);
        if (il >= tl && strcmp(lib->tracks[i].id + il - tl, tail) == 0) return i;
    }
    return -1;
}

static int match_line(const char *m3u_path, const char *line, const MusicLib *lib) {
    char p[1024];
    int absolute = line[0] == '/' || strchr(line, ':') != NULL;
    if (absolute) {
        snprintf(p, sizeof p, "%s", line);
    } else {
        const char *slash = strrchr(m3u_path, '/');
        int dl = slash ? (int)(slash - m3u_path) : 0;
        snprintf(p, sizeof p, "%.*s/%s", dl, m3u_path, line);
    }
    for (char *c = p; *c; c++)
        if (*c == '\\') *c = '/';
    normalize(p);
    lower_ascii(p);
    int t = musiclib_find(lib, p);
    return t >= 0 ? t : find_by_tail(lib, p);
}

int m3u_parse(const char *m3u_path, const char *text, size_t len, const MusicLib *lib, int *out, int max, int *entries) {
    if (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) { text += 3; len -= 3; }
    int utf8 = valid_utf8((const unsigned char *)text, len);
    int n = 0;
    *entries = 0;
    for (size_t i = 0; i < len;) {
        size_t e = i;
        while (e < len && text[e] != '\n' && text[e] != '\r') e++;
        size_t s = i, l = e - i;
        while (l && (text[s] == ' ' || text[s] == '\t')) { s++; l--; }
        while (l && (text[s + l - 1] == ' ' || text[s + l - 1] == '\t')) l--;
        i = e + 1;
        if (!l || text[s] == '#' || l >= 400) continue;
        char line[512];
        if (utf8) {
            memcpy(line, text + s, l);
            line[l] = 0;
        } else {
            text_decode((const uint8_t *)text + s, l, 0, line, sizeof line);
        }
        (*entries)++;
        int t = match_line(m3u_path, line, lib);
        if (t < 0) {                     // the card lists Cyrillic names in Shift-JIS
            char sj[512];
            utf8_to_sjis(line, sj, sizeof sj);
            t = match_line(m3u_path, sj, lib);
        }
        if (t >= 0 && n < max) out[n++] = t;
    }
    return n;
}

void m3u_relpath(char *out, size_t cap, const char *from_dir, const char *to_path) {
    size_t common = 0, i = 0;   // length of the shared folder prefix (ends at a '/' boundary)
    for (; from_dir[i] && to_path[i] && from_dir[i] == to_path[i]; i++)
        if (to_path[i] == '/') common = i + 1;
    if (!from_dir[i] && to_path[i] == '/') common = i + 1;
    size_t ol = 0;
    out[0] = 0;
    if (common <= strlen(from_dir)) {   // one ".." per folder of from_dir below the shared prefix
        for (const char *s = from_dir + common; *s;) {
            ol += (size_t)snprintf(out + ol, ol < cap ? cap - ol : 0, "../");
            const char *e = strchr(s, '/');
            if (!e) break;
            s = e + 1;
        }
    }
    snprintf(out + (ol < cap ? ol : cap - 1), ol < cap ? cap - ol : 1, "%s", to_path + common);
}

int m3u_format(char *buf, size_t cap, const char *m3u_path, const MusicLib *lib, const int *tracks, int n) {
    char dir[256], rel[512];
    const char *slash = strrchr(m3u_path, '/');
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - m3u_path) : 0, m3u_path);
    size_t len = 0;
    int w = snprintf(buf, cap, "#EXTM3U\n");
    if (w < 0 || (size_t)w >= cap) return -1;
    len = (size_t)w;
    for (int i = 0; i < n; i++) {
        const Track *t = &lib->tracks[tracks[i]];
        char raw[512];
        m3u_relpath(raw, sizeof raw, dir, t->path);
        sjis_to_utf8(raw, rel, sizeof rel);   // the PC wants UTF-8, the card lists Shift-JIS
        if (t->artist[0]) w = snprintf(buf + len, cap - len, "#EXTINF:-1,%s - %s\n%s\n", t->artist, t->title, rel);
        else w = snprintf(buf + len, cap - len, "#EXTINF:-1,%s\n%s\n", t->title, rel);
        if (w < 0 || (size_t)w >= cap - len) return -1;
        len += (size_t)w;
    }
    return (int)len;
}

void m3u_safe_name(char *out, size_t cap, const char *name) {
    utf8_copy(out, cap, name[0] ? name : "playlist");
    for (char *c = out; *c; c++)
        if (strchr("\\/:*?\"<>|", *c)) *c = '_';
}
