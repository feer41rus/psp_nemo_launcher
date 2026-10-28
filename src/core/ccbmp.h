#ifndef NEMO_CCBMP_H
#define NEMO_CCBMP_H

#include <stddef.h>
#include <stdint.h>
#include "ccdraw.h"

// Control Center screenshots (1.2.4): 24-bit BMP written row by row (no image library, one row of memory).
int  ccbmp_header(uint8_t out[54], int w, int h);              // bottom-up rows padded to 4 bytes; returns 54
void ccbmp_row(const CcFrame *fb, int y, uint8_t *out);        // frame row y as BGR (w*3 bytes, no padding)
int  ccbmp_name(char *out, size_t cap, const char *game_id, int n);   // ms0:/PICTURE/Nemo/<id>_<nnn>.bmp

#endif
