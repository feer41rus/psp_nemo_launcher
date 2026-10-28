#include "ccdraw.h"
#include <string.h>

const CcGlyph *ccfont_glyph(uint32_t cp) {
    int lo = 0, hi = ccfont_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ccfont_glyphs[mid].cp == cp) return &ccfont_glyphs[mid];
        if (ccfont_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return cp == '?' ? &ccfont_glyphs[0] : ccfont_glyph('?');
}

static uint32_t get(const CcFrame *fb, int x, int y) {
    return fb->fmt == CCPF_8888 ? ((const uint32_t *)fb->base)[y * fb->stride + x]
                                : ((const uint16_t *)fb->base)[y * fb->stride + x];
}

static void put(CcFrame *fb, int x, int y, uint32_t px) {
    if (fb->fmt == CCPF_8888) ((uint32_t *)fb->base)[y * fb->stride + x] = px;
    else ((uint16_t *)fb->base)[y * fb->stride + x] = (uint16_t)px;
}

static void plot(CcFrame *fb, int x, int y, uint32_t rgb, int alpha) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h || alpha <= 0) return;
    put(fb, x, y, ccpix_blend(fb->fmt, get(fb, x, y), rgb, alpha));
}

void ccdraw_dim(CcFrame *fb, int keep) {
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++) put(fb, x, y, ccpix_dim(fb->fmt, get(fb, x, y), keep));
}

void ccdraw_rect(CcFrame *fb, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    for (int j = y < 0 ? 0 : y; j < y + h && j < fb->h; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < fb->w; i++) plot(fb, i, j, rgb, alpha);
}

static int isqrt(int v) {   // integer square root (no libm in the kernel plugin)
    int r = 0, b = 1 << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

// Corners on a circle of radius r. Sixteenths of a pixel: a row's centre is 8/16 below its top.
int ccdraw_round_edge(int w, int h, int r, int j) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    int r16 = r * 16;
    int dy = j < r ? r16 - (j * 16 + 8) : j >= h - r ? (j - (h - r)) * 16 + 8 : -1;
    return dy < 0 ? -1 : r16 - isqrt(r16 * r16 - dy * dy);
}

// Rounded box, the edge pixel of each corner row anti-aliased.
void ccdraw_round(CcFrame *fb, int x, int y, int w, int h, int r, uint32_t rgb, int alpha) {
    for (int j = 0; j < h; j++) {
        int edge = ccdraw_round_edge(w, h, r, j);
        if (edge < 0) {
            ccdraw_rect(fb, x, y + j, w, 1, rgb, alpha);
            continue;
        }
        int inset = edge >> 4, part = 16 - (edge & 15);
        ccdraw_rect(fb, x + inset + 1, y + j, w - 2 * inset - 2, 1, rgb, alpha);
        int a = alpha * part / 16;
        plot(fb, x + inset, y + j, rgb, a);
        plot(fb, x + w - 1 - inset, y + j, rgb, a);
    }
}

// Next code point of a UTF-8 string (invalid bytes come out as '?').
static uint32_t next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c >= 0xe0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xc0 && (p[0] & 0xc0) == 0x80) { c = ((c & 31) << 6) | (p[0] & 63); p++; }
    else if (c >= 0x80) c = '?';
    *s = (const char *)p;
    return c;
}

int ccdraw_text_width(const char *s) {
    int w = 0;
    while (*s) w += ccfont_glyph(next_cp(&s))->adv;
    return w;
}

int ccdraw_text(CcFrame *fb, int x, int y, uint32_t rgb, const char *s) {
    int x0 = x;
    while (*s) {
        const CcGlyph *g = ccfont_glyph(next_cp(&s));
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                int k = j * g->w + i, a = (ccfont_bits[g->off + k / 2] >> ((k & 1) * 4)) & 15;
                plot(fb, x + g->left + i, y + g->top + j, rgb, a * 17);
            }
        x += g->adv;
    }
    return x - x0;
}

static void copy_rows(const CcFrame *fb, int x, int y, int w, int h, void *buf, int out) {
    int bpp = ccpix_bpp(fb->fmt);
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= fb->h) continue;
        int i0 = x < 0 ? -x : 0, i1 = x + w > fb->w ? fb->w - x : w;
        if (i1 <= i0) continue;
        uint8_t *row = (uint8_t *)fb->base + ((size_t)(y + j) * fb->stride + x + i0) * bpp;
        uint8_t *b = (uint8_t *)buf + ((size_t)j * w + i0) * bpp;
        if (out) memcpy(b, row, (size_t)(i1 - i0) * bpp);
        else memcpy(row, b, (size_t)(i1 - i0) * bpp);
    }
}

void ccdraw_save(const CcFrame *fb, int x, int y, int w, int h, void *buf) { copy_rows(fb, x, y, w, h, buf, 1); }
void ccdraw_restore(CcFrame *fb, int x, int y, int w, int h, const void *buf) { copy_rows((CcFrame *)fb, x, y, w, h, (void *)buf, 0); }
