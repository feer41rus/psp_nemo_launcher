#include "ccbmp.h"
#include <stdio.h>
#include <string.h>

static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

int ccbmp_header(uint8_t out[54], int w, int h) {
    uint32_t row = ((uint32_t)w * 3 + 3) & ~3u;
    memset(out, 0, 54);
    out[0] = 'B';
    out[1] = 'M';
    le32(out + 2, 54 + row * (uint32_t)h);
    le32(out + 10, 54);
    le32(out + 14, 40);
    le32(out + 18, (uint32_t)w);
    le32(out + 22, (uint32_t)h);
    out[26] = 1;
    out[28] = 24;
    le32(out + 34, row * (uint32_t)h);
    return 54;
}

void ccbmp_row(const CcFrame *fb, int y, uint8_t *out) {
    for (int x = 0; x < fb->w; x++) {
        uint32_t px = fb->fmt == CCPF_8888 ? ((const uint32_t *)fb->base)[y * fb->stride + x]
                                           : ((const uint16_t *)fb->base)[y * fb->stride + x];
        uint32_t c = ccpix_unpack(fb->fmt, px);
        out[3 * x] = (uint8_t)c;
        out[3 * x + 1] = (uint8_t)(c >> 8);
        out[3 * x + 2] = (uint8_t)(c >> 16);
    }
}

int ccbmp_name(char *out, size_t cap, const char *game_id, int n) {
    return snprintf(out, cap, "ms0:/PICTURE/Nemo/Games/%s_%03d.bmp", game_id && *game_id ? game_id : "GAME", n);
}
