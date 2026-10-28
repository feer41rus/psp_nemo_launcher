#include "ccpix.h"

int ccpix_bpp(CcPixFmt f) { return f == CCPF_8888 ? 4 : 2; }

// Expand an n-bit channel to 8 bits.
static uint32_t up(uint32_t v, int bits) { return bits == 8 ? v : (v << (8 - bits)) | (v >> (2 * bits - 8 > 0 ? 2 * bits - 8 : 0)); }

uint32_t ccpix_pack(CcPixFmt f, uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    switch (f) {
    case CCPF_565: return (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11);
    case CCPF_5551: return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000;
    case CCPF_4444: return (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | 0xf000;
    default: return r | (g << 8) | (b << 16) | 0xff000000u;
    }
}

uint32_t ccpix_unpack(CcPixFmt f, uint32_t px) {
    uint32_t r, g, b;
    switch (f) {
    case CCPF_565: r = up(px & 31, 5); g = up((px >> 5) & 63, 6); b = up((px >> 11) & 31, 5); break;
    case CCPF_5551: r = up(px & 31, 5); g = up((px >> 5) & 31, 5); b = up((px >> 10) & 31, 5); break;
    case CCPF_4444: r = up(px & 15, 4); g = up((px >> 4) & 15, 4); b = up((px >> 8) & 15, 4); break;
    default: r = px & 0xff; g = (px >> 8) & 0xff; b = (px >> 16) & 0xff; break;
    }
    return (r << 16) | (g << 8) | b;
}

uint32_t ccpix_blend(CcPixFmt f, uint32_t px, uint32_t rgb, int alpha) {
    if (alpha <= 0) return px;
    if (alpha >= 255) return ccpix_pack(f, rgb);
    uint32_t c = ccpix_unpack(f, px), out = 0;
    for (int s = 0; s <= 16; s += 8) {
        uint32_t a = (c >> s) & 0xff, b = (rgb >> s) & 0xff;
        out |= ((a * (uint32_t)(255 - alpha) + b * (uint32_t)alpha) / 255) << s;
    }
    return ccpix_pack(f, out);
}

uint32_t ccpix_dim(CcPixFmt f, uint32_t px, int keep) {
    uint32_t c = ccpix_unpack(f, px), out = 0;
    for (int s = 0; s <= 16; s += 8) out |= ((((c >> s) & 0xff) * (uint32_t)keep) / 255) << s;
    return ccpix_pack(f, out);
}
