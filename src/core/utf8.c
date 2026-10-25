#include "utf8.h"

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t utf8_copy(char *dst, size_t cap, const char *src) {
    if (cap == 0) return 0;
    size_t n = 0;
    while (src[n] && n < cap - 1) n++;
    // If we stopped inside a multi-byte sequence, back up to its lead byte.
    if (src[n] && is_cont((unsigned char)src[n])) {
        while (n > 0 && is_cont((unsigned char)src[n])) n--;
    }
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = '\0';
    return n;
}

size_t utf8_prev(const char *s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && is_cont((unsigned char)s[pos])) pos--;
    return pos;
}

// Next code point (invalid bytes are returned as-is), advancing *s.
static unsigned next_cp(const unsigned char **s) {
    const unsigned char *p = *s;
    unsigned c = *p++;
    if (c >= 0xC0 && c < 0xE0 && is_cont(*p)) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    else if (c >= 0xE0 && c < 0xF0 && is_cont(p[0]) && is_cont(p[1])) {
        c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    }
    *s = p;
    return c;
}

static unsigned fold(unsigned c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0x0410 && c <= 0x042F) return c + 32;   // А-Я -> а-я
    if (c >= 0x0400 && c <= 0x040F) return c + 80;   // Ѐ-Џ (incl. Ё) -> ѐ-џ
    return c;
}

int utf8_casecmp(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *y) {
        unsigned cx = fold(next_cp(&x)), cy = fold(next_cp(&y));
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    return (*x != 0) - (*y != 0);
}
