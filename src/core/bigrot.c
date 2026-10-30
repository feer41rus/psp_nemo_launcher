#include "bigrot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A re-encoded photo is often bigger than the original: reserve at least one byte a pixel for it.
long long bigrot_need_bytes(int w, int h, long long src_size) {
    long long px = (long long)w * h;
    return px * 3 + (src_size > px ? src_size : px);
}

// One band of input rows y0..y0+bh-1 (in `band`) into the raw file: each output row gets a run of bh pixels.
static int put_band(FILE *f, const uint8_t *band, int w, int h, int y0, int bh, int cw, uint8_t *seg) {
    for (int dy = 0; dy < w; dy++) {
        int x = cw ? dy : w - 1 - dy;        // input column feeding this output row
        int dx0 = cw ? h - y0 - bh : y0;     // first output column of the run
        for (int k = 0; k < bh; k++) {
            int y = cw ? bh - 1 - k : k;     // clockwise: bottom input row comes first
            memcpy(seg + (size_t)k * 3, band + ((size_t)y * w + x) * 3, 3);
        }
        if (fseek(f, ((long)dy * h + dx0) * 3, SEEK_SET) != 0) return -1;
        if (fwrite(seg, 3, (size_t)bh, f) != (size_t)bh) return -1;
    }
    return 0;
}

int bigrot_rotate(BrSource *src, int w, int h, int cw, const char *raw_path, int band_rows, BrSink *dst, BrProgress *pr) {
    if (w <= 0 || h <= 0) return -1;
    if (band_rows < 1) band_rows = 1;
    if (band_rows > h) band_rows = h;
    int rc = 0;
    pr->percent = 0;
    uint8_t *band = malloc((size_t)band_rows * w * 3), *seg = malloc((size_t)band_rows * 3), *row = malloc((size_t)h * 3);
    FILE *f = fopen(raw_path, "w+b");
    if (!band || !seg || !row || !f) rc = -1;
    for (int y0 = 0; rc == 0 && y0 < h; y0 += band_rows) {
        int bh = h - y0 < band_rows ? h - y0 : band_rows;
        for (int i = 0; i < bh && rc == 0; i++)
            if (src->read_row(src->ctx, band + (size_t)i * w * 3) != 0) rc = -1;
        if (rc == 0 && put_band(f, band, w, h, y0, bh, cw, seg) != 0) rc = -1;
        pr->percent = (int)((long long)(y0 + bh) * 80 / h);
        if (rc == 0 && pr->cancel) rc = -2;
    }
    if (rc == 0 && (fflush(f) != 0 || fseek(f, 0, SEEK_SET) != 0)) rc = -1;
    for (int dy = 0; rc == 0 && dy < w; dy++) {
        if (fread(row, 3, (size_t)h, f) != (size_t)h || dst->write_row(dst->ctx, row) != 0) rc = -1;
        pr->percent = 80 + (int)((long long)(dy + 1) * 20 / w);
        if (rc == 0 && pr->cancel && (dy & 63) == 63) rc = -2;
    }
    if (f) fclose(f);
    remove(raw_path);
    free(band);
    free(seg);
    free(row);
    return rc;
}
