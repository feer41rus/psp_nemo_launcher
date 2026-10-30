#ifndef NEMO_CAMFILTER_H
#define NEMO_CAMFILTER_H

#include <stdint.h>
#include "i18n.h"

// Camera filters (1.3.6) for the viewfinder and photos: RGBA pixels (bytes R, G, B, A), alpha kept.
typedef enum { CF_NONE, CF_FILM, CF_BW, CF_SEPIA, CF_COLD, CF_WARM, CF_COUNT } CamFilter;

// stride in pixels; seed: the film grain (same seed, same grain).
void camfilter_apply(uint8_t *rgba, int w, int h, int stride, CamFilter f, uint32_t seed);
CamFilter camfilter_next(CamFilter f);
StrId camfilter_name(CamFilter f);

#endif
