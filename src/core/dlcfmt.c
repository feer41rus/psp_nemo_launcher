#include "dlcfmt.h"
#include "utf8.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int dlc_pbp_sfo(const uint8_t *pbp, size_t size, size_t *off, size_t *len) {
    if (size < 40 || memcmp(pbp, "\0PBP", 4) != 0) return -1;
    uint32_t a = rd32(pbp + 8), b = rd32(pbp + 12);
    if (a < 40 || b < a || b > size) return -1;
    *off = a;
    *len = b - a;
    return 0;
}

// Title without edge spaces and the ™ / ® marks.
static void norm(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n + 1 < cap;) {
        if (p[0] == 0xE2 && p[1] == 0x84 && p[2] == 0xA2) { p += 3; continue; }   // ™
        if (p[0] == 0xC2 && p[1] == 0xAE) { p += 2; continue; }                   // ®
        out[n++] = (char)*p++;
    }
    out[n] = 0;
    size_t b = 0;
    while (out[b] == ' ') b++;
    memmove(out, out + b, n - b + 1);
    n -= b;
    while (n && out[n - 1] == ' ') out[--n] = 0;
}

int dlc_title_eq(const char *a, const char *b) {
    char x[160], y[160];
    norm(a, x, sizeof x);
    norm(b, y, sizeof y);
    return x[0] && utf8_casecmp(x, y) == 0;
}

static void copy_cid(const uint8_t *p, char cid[37]) {
    memcpy(cid, p, 36);
    cid[36] = 0;
}

int dlc_encrypted(const uint8_t *head, size_t n, char cid[37]) {
    cid[0] = 0;
    if (n >= 0x10 + 36 && memcmp(head, "NPD\0", 4) == 0) {
        copy_cid(head + 0x10, cid);
        return 1;
    }
    if (n >= 8 && memcmp(head, "\0PSPEDAT", 8) == 0) {
        if (n >= 0xA0 + 36 && memcmp(head + 0x90, "NPD\0", 4) == 0) copy_cid(head + 0xA0, cid);
        return 1;
    }
    return 0;
}
