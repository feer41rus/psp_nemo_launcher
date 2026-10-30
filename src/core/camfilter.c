#include "camfilter.h"

static uint8_t clamp(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

// Film: faded blacks, a warm cast, softer colour, grain and a vignette — an old print.
static void film(uint8_t *rgba, int w, int h, int stride, uint32_t seed) {
    int cx = w / 2, cy = h / 2;
    int r2 = cx * cx + cy * cy;
    if (r2 <= 0) r2 = 1;
    // The vignette is separable (dx² + dy²): a column table once per call, no division per pixel
    // (perf review 1.3.6: a divide per pixel cost ~16 ms a viewfinder frame at 333 MHz).
    static int col[2048];
    int wc = w < 2048 ? w : 2048;
    for (int x = 0; x < wc; x++) col[x] = 112 * (x - cx) * (x - cx) / r2;
    uint32_t rnd = seed * 2654435761u + 1;
    for (int y = 0; y < h; y++) {
        uint8_t *p = rgba + (long)y * stride * 4;
        int dy = y - cy, row = 112 * dy * dy / r2;
        for (int x = 0; x < w; x++, p += 4) {
            int vig = 256 - (x < wc ? col[x] : 112) - row;   // 256 in the middle, ~144 in the corners
            rnd = rnd * 1664525u + 1013904223u;
            int grain = (int)((rnd >> 24) & 15) - 8;
            int l = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            int r = (p[0] * 3 + l) / 4, g = (p[1] * 3 + l) / 4, b = (p[2] * 3 + l) / 4;   // a quarter less colour
            r = 24 + r * 220 / 256 + 10;
            g = 22 + g * 216 / 256 + 4;
            b = 20 + b * 200 / 256;
            p[0] = clamp(r * vig / 256 + grain);
            p[1] = clamp(g * vig / 256 + grain);
            p[2] = clamp(b * vig / 256 + grain);
        }
    }
}

void camfilter_apply(uint8_t *rgba, int w, int h, int stride, CamFilter f, uint32_t seed) {
    if (f == CF_NONE || f >= CF_COUNT) return;
    if (f == CF_FILM) { film(rgba, w, h, stride, seed); return; }
    for (int y = 0; y < h; y++) {
        uint8_t *p = rgba + (long)y * stride * 4;
        for (int x = 0; x < w; x++, p += 4) {
            int r = p[0], g = p[1], b = p[2];
            int l = (77 * r + 150 * g + 29 * b) >> 8;
            switch (f) {
            case CF_BW: p[0] = p[1] = p[2] = (uint8_t)l; break;
            case CF_SEPIA:
                p[0] = clamp(l + 40);
                p[1] = clamp(l + 14);
                p[2] = clamp(l - 24);
                break;
            case CF_COLD:
                p[0] = clamp(r - 18);
                p[1] = clamp(g + 2);
                p[2] = clamp(b + 26);
                break;
            case CF_WARM:
                p[0] = clamp(r + 24);
                p[1] = clamp(g + 8);
                p[2] = clamp(b - 20);
                break;
            default: break;
            }
        }
    }
}

CamFilter camfilter_next(CamFilter f) { return (CamFilter)((f + 1) % CF_COUNT); }

StrId camfilter_name(CamFilter f) {
    static const StrId N[CF_COUNT] = { S_CAM_F_NONE, S_CAM_F_FILM, S_CAM_F_BW, S_CAM_F_SEPIA, S_CAM_F_COLD, S_CAM_F_WARM };
    return f < CF_COUNT ? N[f] : S_CAM_F_NONE;
}
