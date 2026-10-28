#ifndef NEMO_CCPIX_H
#define NEMO_CCPIX_H

#include <stdint.h>

// Pixels of a PSP frame buffer (Control Center, 1.2.4). Formats as PSP_DISPLAY_PIXEL_FORMAT_*; red sits in
// the low bits. Colours are given as 0xRRGGBB.
typedef enum { CCPF_565 = 0, CCPF_5551 = 1, CCPF_4444 = 2, CCPF_8888 = 3 } CcPixFmt;

int      ccpix_bpp(CcPixFmt f);                                      // bytes per pixel
uint32_t ccpix_pack(CcPixFmt f, uint32_t rgb);
uint32_t ccpix_unpack(CcPixFmt f, uint32_t px);
uint32_t ccpix_blend(CcPixFmt f, uint32_t px, uint32_t rgb, int alpha);   // alpha 0..255
uint32_t ccpix_dim(CcPixFmt f, uint32_t px, int keep);                    // keep 0..255 of the brightness

#endif
