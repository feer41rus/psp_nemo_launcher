#include "libbg.h"
#include <string.h>

#define BLUR_R 3        // two box passes of radius 3 ~ a Gaussian with sigma 2.8
#define UI_R 14         // the UI background (COL_BG 0x0e131b) the picture is pulled toward
#define UI_G 19
#define UI_B 27
#define KEEP 55         // % of the picture kept after the pull
#define SAT 85          // % of the colour kept
#define GRAIN 3         // +- grain in levels

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int libbg_valid(int v) { return v >= 0 && v < LIBBG_COUNT ? v : LIBBG_BRANCHES; }

StrId libbg_name(int v) {
    static const StrId NAMES[LIBBG_COUNT] = { S_LIBBG_BRANCHES, S_LIBBG_RAILS, S_LIBBG_THREE_LIVES, S_LIBBG_PARTICLES,
                                              S_LIBBG_MARS, S_LIBBG_GRID, S_LIBBG_OWN };
    return NAMES[libbg_valid(v)];
}

void libbg_cover(const uint8_t *rgb, int w, int h, uint8_t *out) {
    // Scale so the picture covers the screen; crop the overflow evenly on both sides (16.16 fixed point).
    int64_t sx = (int64_t)w * 65536 / LIBBG_W, sy = (int64_t)h * 65536 / LIBBG_H;
    int64_t step = sx < sy ? sx : sy;                       // source pixels per screen pixel
    int64_t ox = ((int64_t)w * 65536 - step * LIBBG_W) / 2, oy = ((int64_t)h * 65536 - step * LIBBG_H) / 2;
    for (int y = 0; y < LIBBG_H; y++) {
        int y0 = clampi((int)((oy + step * y) >> 16), 0, h - 1);
        int y1 = clampi((int)((oy + step * (y + 1) + 65535) >> 16), y0 + 1, h);
        for (int x = 0; x < LIBBG_W; x++) {
            int x0 = clampi((int)((ox + step * x) >> 16), 0, w - 1);
            int x1 = clampi((int)((ox + step * (x + 1) + 65535) >> 16), x0 + 1, w);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t *p = rgb + ((size_t)yy * w + x0) * 3;
                for (int xx = x0; xx < x1; xx++, p += 3) {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    n++;
                }
            }
            uint8_t *o = out + ((size_t)y * LIBBG_W + x) * 3;
            o[0] = (uint8_t)(r / n);
            o[1] = (uint8_t)(g / n);
            o[2] = (uint8_t)(b / n);
        }
    }
}

// One box pass along a line of n pixels, `step` bytes apart (edges repeat the end pixel).
static void box_line(const uint8_t *src, uint8_t *dst, int n, size_t step) {
    for (int c = 0; c < 3; c++) {
        int sum = 0;
        for (int i = -BLUR_R; i <= BLUR_R; i++) sum += src[(size_t)clampi(i, 0, n - 1) * step + c];
        for (int i = 0; i < n; i++) {
            dst[(size_t)i * step + c] = (uint8_t)(sum / (2 * BLUR_R + 1));
            sum += src[(size_t)clampi(i + BLUR_R + 1, 0, n - 1) * step + c] - src[(size_t)clampi(i - BLUR_R, 0, n - 1) * step + c];
        }
    }
}

static void box_blur(uint8_t *rgb, uint8_t *tmp) {
    for (int y = 0; y < LIBBG_H; y++)
        box_line(rgb + (size_t)y * LIBBG_W * 3, tmp + (size_t)y * LIBBG_W * 3, LIBBG_W, 3);
    for (int x = 0; x < LIBBG_W; x++) box_line(tmp + (size_t)x * 3, rgb + (size_t)x * 3, LIBBG_H, (size_t)LIBBG_W * 3);
}

void libbg_matte(uint8_t *rgb, uint8_t *tmp) {
    box_blur(rgb, tmp);
    box_blur(rgb, tmp);
    uint32_t seed = 0x6e656d6fu;   // fixed: the same picture always gets the same grain
    for (size_t i = 0; i < (size_t)LIBBG_W * LIBBG_H; i++) {
        uint8_t *p = rgb + i * 3;
        int r = (p[0] * KEEP + UI_R * (100 - KEEP)) / 100;
        int g = (p[1] * KEEP + UI_G * (100 - KEEP)) / 100;
        int b = (p[2] * KEEP + UI_B * (100 - KEEP)) / 100;
        int lum = (r + g + b) / 3;
        seed = seed * 1664525u + 1013904223u;
        int n = (int)(seed >> 24) % (2 * GRAIN + 1) - GRAIN;
        p[0] = (uint8_t)clampi(lum + (r - lum) * SAT / 100 + n, 0, 255);
        p[1] = (uint8_t)clampi(lum + (g - lum) * SAT / 100 + n, 0, 255);
        p[2] = (uint8_t)clampi(lum + (b - lum) * SAT / 100 + n, 0, 255);
    }
}
