#ifndef NEMO_LIBBG_H
#define NEMO_LIBBG_H

#include <stdint.h>
#include "i18n.h"

// Library background (1.3.8): any picture from the Gallery becomes the Library tab's backdrop,
// cut to the screen and given a matte coat so shelf titles stay readable. Pixels are packed RGB.
// Settings › Nemo › Library background: Nemo's pictures (assets/backgrounds, their meaning in its README),
// then the user's own made in the Gallery.
typedef enum { LIBBG_BRANCHES, LIBBG_RAILS, LIBBG_THREE_LIVES, LIBBG_PARTICLES, LIBBG_MARS, LIBBG_GRID, LIBBG_OWN,
               LIBBG_COUNT } LibBg;
int libbg_valid(int v);        // an unknown value (newer or broken file) becomes the first picture
StrId libbg_name(int v);

#define LIBBG_W 480
#define LIBBG_H 272

// Scale to cover LIBBG_W x LIBBG_H (centre crop, box-averaged). out: LIBBG_W * LIBBG_H * 3 bytes.
void libbg_cover(const uint8_t *rgb, int w, int h, uint8_t *out);
// The matte coat in place: soft blur, a pull toward the UI's dark blue, softer colours, fine grain.
// tmp: LIBBG_W * LIBBG_H * 3 bytes of scratch.
void libbg_matte(uint8_t *rgb, uint8_t *tmp);

#endif
