#include "flacsrc.h"
#include <stdlib.h>
#include <string.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

#define IN_FRAMES 1024

struct FlacSrc {
    drflac *f;
    uint32_t step;              // input frames per output frame, 16.16 fixed point
    uint32_t frac;              // position between a and b, 16.16
    int16_t a[2], b[2];         // the two input frames being interpolated
    int primed, ended;
    int16_t raw[2 * IN_FRAMES]; // as decoded (1 or 2 channels); kept off the decoder thread stack
    int16_t in[2 * IN_FRAMES];  // decoded input, stereo
    uint32_t in_len, in_pos;
};

// Next input frame as stereo into out; 0 at the end of the stream.
static int next_frame(FlacSrc *s, int16_t *out) {
    if (s->in_pos == s->in_len) {
        int16_t *raw = s->raw;
        uint32_t ch = s->f->channels;
        drflac_uint64 got = drflac_read_pcm_frames_s16(s->f, IN_FRAMES, raw);
        if (got == 0) return 0;
        for (uint32_t i = 0; i < got; i++) {
            s->in[2 * i] = raw[ch * i];
            s->in[2 * i + 1] = raw[ch * i + ch - 1];   // mono: the same sample twice
        }
        s->in_len = (uint32_t)got;
        s->in_pos = 0;
    }
    out[0] = s->in[2 * s->in_pos];
    out[1] = s->in[2 * s->in_pos + 1];
    s->in_pos++;
    return 1;
}

FlacSrc *flacsrc_open(const char *path, uint32_t start_ms) {
    drflac *f = drflac_open_file(path, NULL);
    if (!f) return NULL;
    if (f->channels < 1 || f->channels > 2 || f->sampleRate == 0 || f->sampleRate > 48000 || f->bitsPerSample > 24) {
        drflac_close(f);
        return NULL;
    }
    FlacSrc *s = calloc(1, sizeof *s);
    if (!s) {
        drflac_close(f);
        return NULL;
    }
    s->f = f;
    s->step = (uint32_t)(((uint64_t)f->sampleRate << 16) / FLACSRC_RATE);
    if (start_ms && !drflac_seek_to_pcm_frame(f, (drflac_uint64)start_ms * f->sampleRate / 1000)) {
        flacsrc_close(s);
        return NULL;
    }
    return s;
}

uint32_t flacsrc_read(FlacSrc *s, int16_t *dst, uint32_t frames) {
    uint32_t n = 0;
    if (s->step == 1u << 16) {                    // 44100 Hz: no resampling
        while (n < frames && next_frame(s, dst + 2 * n)) n++;
        return n;
    }
    if (!s->primed) {
        s->primed = 1;
        if (!next_frame(s, s->a) || !next_frame(s, s->b)) s->ended = 1;
    }
    while (n < frames && !s->ended) {
        for (int c = 0; c < 2; c++)
            dst[2 * n + c] = (int16_t)(s->a[c] + (((int32_t)s->b[c] - s->a[c]) * (int32_t)(s->frac >> 1) >> 15));
        n++;
        s->frac += s->step;
        while (s->frac >= 1u << 16 && !s->ended) {
            s->frac -= 1u << 16;
            memcpy(s->a, s->b, sizeof s->a);
            if (!next_frame(s, s->b)) s->ended = 1;
        }
    }
    return n;
}

uint32_t flacsrc_duration_ms(const FlacSrc *s) {
    return (uint32_t)(s->f->totalPCMFrameCount * 1000 / s->f->sampleRate);
}

void flacsrc_close(FlacSrc *s) {
    if (!s) return;
    drflac_close(s->f);
    free(s);
}
