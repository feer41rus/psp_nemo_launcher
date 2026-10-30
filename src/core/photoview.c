#include "photoview.h"
#include <string.h>

PvRect pv_fit(int img_w, int img_h, int box_w, int box_h) {
    PvRect r = { 0, 0, box_w, box_h };
    if (img_w <= 0 || img_h <= 0) return r;
    if ((long long)img_w * box_h >= (long long)img_h * box_w) {   // width-limited
        r.w = box_w;
        r.h = (int)(((long long)img_h * box_w + img_w / 2) / img_w);
    } else {
        r.h = box_h;
        r.w = (int)(((long long)img_w * box_h + img_h / 2) / img_h);
    }
    r.x = (box_w - r.w) / 2;
    r.y = (box_h - r.h) / 2;
    return r;
}

int pv_zoom_steps(int img_w, int img_h, int box_w, int box_h, float out[5]) {
    static const float STEPS[5] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    PvRect f = pv_fit(img_w, img_h, box_w, box_h);
    float limit = f.w > 0 ? (float)img_w / (float)f.w : 1.0f;
    int n = 0;
    for (int i = 0; i < 5; i++)
        if (i == 0 || STEPS[i] <= limit + 0.01f) out[n++] = STEPS[i];
    return n;
}

static int clamp_axis(int v, int shown, int box) {
    int half = shown > box ? (shown - box) / 2 : 0;
    return v > half ? half : v < -half ? -half : v;
}

void pv_clamp_pan(int img_w, int img_h, int box_w, int box_h, float zoom, int *pan_x, int *pan_y) {
    PvRect f = pv_fit(img_w, img_h, box_w, box_h);
    *pan_x = clamp_axis(*pan_x, (int)(f.w * zoom + 0.5f), box_w);
    *pan_y = clamp_axis(*pan_y, (int)(f.h * zoom + 0.5f), box_h);
}

int pv_decode_denom(int w, int h, int max_side) {
    int side = w > h ? w : h;
    for (int d = 1; d < 8; d *= 2)
        if ((side + d - 1) / d <= max_side) return d;
    return 8;
}

int pv_rotate(const uint8_t *src, int w, int h, int bpp, int cw, uint8_t *dst) {
    if (!src || !dst || w <= 0 || h <= 0 || (bpp != 3 && bpp != 4)) return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx = cw ? h - 1 - y : y, dy = cw ? x : w - 1 - x;   // result is h wide
            memcpy(&dst[((size_t)dy * h + dx) * bpp], &src[((size_t)y * w + x) * bpp], (size_t)bpp);
        }
    return 0;
}
