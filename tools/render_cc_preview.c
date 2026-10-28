// Control Center preview for the README collage (1.2.5): draws the plugin's window over a game screenshot
// (a 24-bit BMP from PICTURE/Nemo) with the plugin's own drawing code, so it looks exactly as on the PSP.
//   build/render_cc_preview in.bmp out.ppm [en]
#include "../src/cc/ccui.h"
#include "../src/core/ccdraw.h"
#include "../src/core/ccmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t px[272][480];

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in.bmp out.ppm [en]\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint8_t h[54], row[480 * 3];
    if (fread(h, 1, 54, f) != 54) return 1;
    for (int y = 271; y >= 0; y--) {   // bottom-up BGR rows
        if (fread(row, 1, sizeof row, f) != sizeof row) return 1;
        for (int x = 0; x < 480; x++) px[y][x] = ccpix_pack(CCPF_8888, (uint32_t)row[3 * x + 2] << 16 | row[3 * x + 1] << 8 | row[3 * x]);
    }
    fclose(f);
    int en = argc > 3 && strcmp(argv[3], "en") == 0;
    CcFrame fb = { px, 480, 480, 272, CCPF_8888 };
    ccdraw_dim(&fb, 96);
    CcMenu m;
    ccmenu_init(&m, 1);
    ccmenu_open(&m, 18, 2);
    m.sel = CCI_VOLUME;
    ccui_draw(&fb, CCUI_X, CCUI_Y, &m, en, 1, 21, 45, 78);
    FILE *o = fopen(argv[2], "wb");
    if (!o) return 1;
    fprintf(o, "P6\n480 272\n255\n");
    for (int y = 0; y < 272; y++)
        for (int x = 0; x < 480; x++) {
            uint32_t c = ccpix_unpack(CCPF_8888, px[y][x]);
            fputc(c >> 16, o); fputc((c >> 8) & 255, o); fputc(c & 255, o);
        }
    fclose(o);
    return 0;
}
