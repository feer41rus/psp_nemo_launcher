#ifndef NEMO_PHOTOVIEW_H
#define NEMO_PHOTOVIEW_H

#include <stdint.h>

// Photo viewer maths (1.3.1): fit to the screen, zoom steps, pan limits, decode scale, pixel rotation.
typedef struct { int x, y, w, h; } PvRect;

PvRect pv_fit(int img_w, int img_h, int box_w, int box_h);   // keep proportions, centred
// Steps of {1, 1.5, 2, 3, 4} not beyond the loaded resolution (1 always); returns how many.
int  pv_zoom_steps(int img_w, int img_h, int box_w, int box_h, float out[5]);
// Pan = offset of the view centre in screen pixels; clamped so no picture edge comes inside the screen.
void pv_clamp_pan(int img_w, int img_h, int box_w, int box_h, float zoom, int *pan_x, int *pan_y);
int  pv_decode_denom(int w, int h, int max_side);   // 1, 2, 4 or 8 for a scaled JPEG decode
// Rotate a packed buffer (bpp 3 or 4) by 90 degrees: cw = 1 clockwise, 0 counter-clockwise; dst is h x w.
int  pv_rotate(const uint8_t *src, int w, int h, int bpp, int cw, uint8_t *dst);

#endif
