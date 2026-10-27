#include "pcmring.h"
#include <stdlib.h>
#include <string.h>

int pcmring_init(PcmRing *r, uint32_t cap_frames) {
    r->buf = malloc((size_t)cap_frames * 2 * sizeof(int16_t));
    r->cap_frames = r->buf ? cap_frames : 0;
    r->wr = r->rd = 0;
    return r->buf ? 0 : -1;
}

void pcmring_free(PcmRing *r) {
    free(r->buf);
    r->buf = NULL;
    r->cap_frames = 0;
}

uint32_t pcmring_fill(const PcmRing *r) { return r->wr - r->rd; }

void pcmring_clear(PcmRing *r) { r->wr = r->rd = 0; }

uint32_t pcmring_write(PcmRing *r, const int16_t *src, uint32_t frames) {
    uint32_t space = r->cap_frames - pcmring_fill(r);
    if (frames > space) frames = space;
    for (uint32_t done = 0; done < frames;) {
        uint32_t at = (r->wr + done) % r->cap_frames;
        uint32_t run = r->cap_frames - at;
        if (run > frames - done) run = frames - done;
        memcpy(r->buf + 2 * at, src + 2 * done, run * 2 * sizeof(int16_t));
        done += run;
    }
    r->wr += frames;   // publish after the data is in place
    return frames;
}

uint32_t pcmring_read(PcmRing *r, int16_t *dst, uint32_t frames) {
    uint32_t fill = pcmring_fill(r);
    if (frames > fill) frames = fill;
    for (uint32_t done = 0; done < frames;) {
        uint32_t at = (r->rd + done) % r->cap_frames;
        uint32_t run = r->cap_frames - at;
        if (run > frames - done) run = frames - done;
        memcpy(dst + 2 * done, r->buf + 2 * at, run * 2 * sizeof(int16_t));
        done += run;
    }
    r->rd += frames;
    return frames;
}
