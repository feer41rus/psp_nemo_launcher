#include "pcmrs.h"

void pcmrs_init(PcmRs *r, int rate, int channels, PcmFormat fmt) {
    r->rate = rate > 0 ? rate : 44100;
    r->channels = channels == 2 ? 2 : 1;
    r->fmt = fmt;
    r->step = (uint32_t)(((uint64_t)r->rate << 16) / 44100);
    r->frac = 0;
}

static int16_t mulaw(uint8_t u) {
    u = (uint8_t)~u;
    int t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (int16_t)((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

static int16_t sample(const PcmRs *r, const void *in, int frame, int ch) {
    int i = frame * r->channels + (r->channels == 2 ? ch : 0);
    switch (r->fmt) {
    case PCMF_U8: return (int16_t)((((const uint8_t *)in)[i] - 128) * 256);
    case PCMF_MULAW: return mulaw(((const uint8_t *)in)[i]);
    default: return ((const int16_t *)in)[i];
    }
}

int pcmrs_run(PcmRs *r, const void *in, int in_frames, int *used, int16_t *out, int out_frames) {
    int o = 0, base = 0;
    while (o < out_frames) {
        if (base + 1 >= in_frames) break;   // the next frame is needed to interpolate
        for (int ch = 0; ch < 2; ch++) {
            int a = sample(r, in, base, ch), b = sample(r, in, base + 1, ch);
            out[2 * o + ch] = (int16_t)(a + (int)(((int64_t)(b - a) * r->frac) >> 16));
        }
        o++;
        r->frac += r->step;
        base += (int)(r->frac >> 16);
        r->frac &= 0xFFFF;
    }
    *used = base > in_frames ? in_frames : base;   // review M3: a big step may pass the end
    return o;
}
