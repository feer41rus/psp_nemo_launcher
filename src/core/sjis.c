#include "sjis.h"
#include <stdint.h>
#include <string.h>

static int valid_utf8(const unsigned char *s) {
    while (*s) {
        unsigned char c = *s;
        int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!k) return 0;
        for (int j = 1; j < k; j++)
            if ((s[j] & 0xC0) != 0x80) return 0;
        s += k;
    }
    return 1;
}

// Shift-JIS row 0x84: А..Е Ё Ж..Я at 0x40..0x60, а..е ё ж..н at 0x70..0x7E, о..я at 0x80..0x91.
static uint32_t cyr_from_sjis(unsigned char t) {
    if (t >= 0x40 && t <= 0x60) return t < 0x46 ? 0x410 + (t - 0x40) : t == 0x46 ? 0x401 : 0x416 + (t - 0x47);
    if (t >= 0x70 && t <= 0x91 && t != 0x7F) {
        if (t < 0x76) return 0x430 + (t - 0x70);
        if (t == 0x76) return 0x451;
        return 0x436 + (t - 0x77) - (t > 0x7F ? 1 : 0);
    }
    return 0;
}

static int sjis_from_cyr(uint32_t u) {
    if (u == 0x401) return 0x46;
    if (u == 0x451) return 0x76;
    if (u >= 0x410 && u <= 0x415) return 0x40 + (int)(u - 0x410);
    if (u >= 0x416 && u <= 0x42F) return 0x47 + (int)(u - 0x416);
    if (u >= 0x430 && u <= 0x435) return 0x70 + (int)(u - 0x430);
    if (u >= 0x436 && u <= 0x43D) return 0x77 + (int)(u - 0x436);
    if (u >= 0x43E && u <= 0x44F) return 0x80 + (int)(u - 0x43E);
    return -1;
}

void sjis_to_utf8(const char *in, char *out, size_t cap) {
    if (!cap) return;
    const unsigned char *s = (const unsigned char *)in;
    size_t n = 0;
    if (valid_utf8(s)) {   // copy without splitting a character
        size_t len = strlen(in);
        if (len >= cap) {
            len = cap - 1;
            while (len && (s[len] & 0xC0) == 0x80) len--;
        }
        memcpy(out, in, len);
        out[len] = 0;
        return;
    }
    while (*s) {
        if (*s < 0x80) {
            if (n + 1 >= cap) break;
            out[n++] = (char)*s++;
            continue;
        }
        int lead = (*s >= 0x81 && *s <= 0x9F) || (*s >= 0xE0 && *s <= 0xFC);
        uint32_t u = lead && s[0] == 0x84 && s[1] ? cyr_from_sjis(s[1]) : 0;
        s += lead && s[1] ? 2 : 1;
        if (u) {
            if (n + 2 >= cap) break;
            out[n++] = (char)(0xC0 | (u >> 6));
            out[n++] = (char)(0x80 | (u & 0x3F));
        } else {
            if (n + 1 >= cap) break;
            out[n++] = '?';
        }
    }
    out[n] = 0;
}

void utf8_to_sjis(const char *in, char *out, size_t cap) {
    if (!cap) return;
    const unsigned char *s = (const unsigned char *)in;
    size_t n = 0;
    while (*s) {
        uint32_t u;
        int k;
        if (*s < 0x80) { u = *s; k = 1; }
        else if ((*s & 0xE0) == 0xC0 && s[1]) { u = ((uint32_t)(*s & 0x1F) << 6) | (s[1] & 0x3F); k = 2; }
        else { u = 0xFFFD; k = (*s & 0xF0) == 0xE0 ? 3 : (*s & 0xF8) == 0xF0 ? 4 : 1; }
        for (int j = 1; j < k; j++)
            if (!s[j]) { k = j; break; }
        s += k;
        if (u < 0x80) {
            if (n + 1 >= cap) break;
            out[n++] = (char)u;
            continue;
        }
        int t = sjis_from_cyr(u);
        if (t >= 0) {
            if (n + 2 >= cap) break;
            out[n++] = (char)0x84;
            out[n++] = (char)t;
        } else {
            if (n + 1 >= cap) break;
            out[n++] = '_';
        }
    }
    out[n] = 0;
}
