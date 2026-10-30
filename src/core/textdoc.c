#include "textdoc.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>

static char *dup_n(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static int grow(TextDoc *d, int need) {
    if (need <= d->cap) return 0;
    int cap = d->cap ? d->cap : 64;
    while (cap < need) cap *= 2;
    char **l = realloc(d->lines, (size_t)cap * sizeof *l);
    if (!l) return -1;
    d->lines = l;
    unsigned char *c = realloc(d->changed, (size_t)cap);
    if (!c) return -1;
    d->changed = c;
    unsigned char *e = realloc(d->eol, (size_t)cap);
    if (!e) return -1;
    d->eol = e;
    d->cap = cap;
    return 0;
}

static int push(TextDoc *d, const char *s, size_t n, int eol) {
    if (grow(d, d->count + 1) != 0) return -1;
    char *p = dup_n(s, n);
    if (!p) return -1;
    d->lines[d->count] = p;
    d->changed[d->count] = 0;
    d->eol[d->count] = (unsigned char)eol;
    d->count++;
    return 0;
}

int textdoc_parse(TextDoc *d, const char *data, size_t n) {
    memset(d, 0, sizeof *d);
    if (memchr(data, '\0', n)) return -1;
    if (n >= 3 && !memcmp(data, "\xEF\xBB\xBF", 3)) {
        d->bom = 1;
        data += 3;
        n -= 3;
    }
    const char *nl = memchr(data, '\n', n);
    d->crlf = nl && nl > data && nl[-1] == '\r';
    d->final_nl = n > 0 && data[n - 1] == '\n';
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && data[i] != '\n') continue;
        if (i == n && d->final_nl) break;   // nothing after the last newline
        size_t len = i - start;
        int eol = i < n ? 1 : 0;
        if (eol && len && data[start + len - 1] == '\r') { len--; eol = 2; }
        if (push(d, data + start, len, eol) != 0) { textdoc_free(d); return -1; }
        start = i + 1;
    }
    if (d->count == 0 && push(d, "", 0, 0) != 0) { textdoc_free(d); return -1; }
    return 0;
}

char *textdoc_join(const TextDoc *d, size_t *n) {
    size_t total = d->bom ? 3 : 0;
    for (int i = 0; i < d->count; i++) total += strlen(d->lines[i]) + d->eol[i];
    char *out = malloc(total + 1), *p = out;
    if (!out) return NULL;
    if (d->bom) { memcpy(p, "\xEF\xBB\xBF", 3); p += 3; }
    for (int i = 0; i < d->count; i++) {
        size_t len = strlen(d->lines[i]);
        memcpy(p, d->lines[i], len);
        p += len;
        if (d->eol[i] == 2) *p++ = '\r';
        if (d->eol[i]) *p++ = '\n';
    }
    *p = '\0';
    if (n) *n = (size_t)(p - out);
    return out;
}

int textdoc_set(TextDoc *d, int line, const char *text) {
    if (line < 0 || line >= d->count) return -1;
    char *p = dup_n(text, strlen(text));
    if (!p) return -1;
    free(d->lines[line]);
    d->lines[line] = p;
    d->changed[line] = 1;
    d->dirty = 1;
    return 0;
}

int textdoc_insert(TextDoc *d, int after, const char *text) {
    if (after < -1 || after >= d->count || grow(d, d->count + 1) != 0) return -1;
    char *p = dup_n(text, strlen(text));
    if (!p) return -1;
    int at = after + 1;
    memmove(d->lines + at + 1, d->lines + at, (size_t)(d->count - at) * sizeof *d->lines);
    memmove(d->changed + at + 1, d->changed + at, (size_t)(d->count - at));
    memmove(d->eol + at + 1, d->eol + at, (size_t)(d->count - at));
    int dominant = d->crlf ? 2 : 1;
    if (at == d->count) {   // below the last line: it now needs an ending, the new one takes its "none"
        d->eol[at] = d->eol[at - 1];
        d->eol[at - 1] = (unsigned char)dominant;
    } else {
        d->eol[at] = (unsigned char)dominant;
    }
    d->lines[at] = p;
    d->changed[at] = 1;
    d->count++;
    d->dirty = 1;
    return 0;
}

int textdoc_delete(TextDoc *d, int line) {
    if (line < 0 || line >= d->count) return -1;
    if (d->count == 1) return textdoc_set(d, 0, "");
    free(d->lines[line]);
    if (line == d->count - 1) d->eol[line - 1] = d->eol[line];   // the new last line ends as the file did
    memmove(d->lines + line, d->lines + line + 1, (size_t)(d->count - line - 1) * sizeof *d->lines);
    memmove(d->changed + line, d->changed + line + 1, (size_t)(d->count - line - 1));
    memmove(d->eol + line, d->eol + line + 1, (size_t)(d->count - line - 1));
    d->count--;
    d->dirty = 1;
    return 0;
}

int textdoc_find(const TextDoc *d, int from, const char *needle) {
    if (d->count <= 0) return -1;
    if (from < 0 || from >= d->count) from = 0;
    for (int k = 0; k < d->count; k++) {
        int i = (from + k) % d->count;
        if (utf8_casefind(d->lines[i], needle) >= 0) return i;
    }
    return -1;
}

void textdoc_free(TextDoc *d) {
    for (int i = 0; i < d->count; i++) free(d->lines[i]);
    free(d->lines);
    free(d->changed);
    free(d->eol);
    memset(d, 0, sizeof *d);
}
