#ifndef NEMO_CCDRAW_H
#define NEMO_CCDRAW_H

#include <stddef.h>
#include <stdint.h>
#include "ccfont.h"
#include "ccpix.h"

// Drawing straight into a frame buffer (Control Center, 1.2.4): no allocation, everything clipped.
typedef struct { void *base; int stride; int w, h; CcPixFmt fmt; } CcFrame;   // stride in pixels

void ccdraw_dim(CcFrame *fb, int keep);
void ccdraw_rect(CcFrame *fb, int x, int y, int w, int h, uint32_t rgb, int alpha);
void ccdraw_round(CcFrame *fb, int x, int y, int w, int h, int r, uint32_t rgb, int alpha);
// Row j of a w x h box rounded by r: how far its edge is in from the sides, in 1/16 px; -1 = full row.
int  ccdraw_round_edge(int w, int h, int r, int j);
int  ccdraw_text(CcFrame *fb, int x, int y, uint32_t rgb, const char *utf8);   // returns the width
int  ccdraw_text_width(const char *utf8);
// Copy a rectangle out of / back into the frame; buf holds w*h*bpp bytes.
void ccdraw_save(const CcFrame *fb, int x, int y, int w, int h, void *buf);
void ccdraw_restore(CcFrame *fb, int x, int y, int w, int h, const void *buf);

#endif
