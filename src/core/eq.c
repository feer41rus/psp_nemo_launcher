#include "eq.h"
#include <math.h>

const int EQ_FREQS[EQ_BANDS] = { 60, 230, 910, 3600, 14000 };

static const int PRESETS[EQP_CUSTOM][EQ_BANDS] = {
    [EQP_FLAT] = { 0, 0, 0, 0, 0 },
    [EQP_BASS] = { 6, 4, 0, 0, 0 },
    [EQP_ROCK] = { 4, 2, -1, 2, 4 },
    [EQP_POP] = { -1, 2, 4, 2, -1 },
    [EQP_JAZZ] = { 3, 1, -1, 1, 3 },
    [EQP_CLASSIC] = { 4, 2, 0, 2, 3 },
    [EQP_VOCAL] = { -2, 0, 3, 4, 1 },
};

void eq_preset_gains(EqPreset p, const int custom[EQ_BANDS], int out[EQ_BANDS]) {
    for (int i = 0; i < EQ_BANDS; i++)
        out[i] = p == EQP_CUSTOM ? (custom ? custom[i] : 0) : (p >= 0 && p < EQP_CUSTOM ? PRESETS[p][i] : 0);
}

int eq_is_flat(const int gains[EQ_BANDS]) {
    for (int i = 0; i < EQ_BANDS; i++)
        if (gains[i]) return 0;
    return 1;
}

// RBJ Audio EQ Cookbook; kind 0 = low shelf, 1 = peak, 2 = high shelf (shelf slope 1).
static EqBiquad design(int kind, float hz, float db, int rate) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * (float)M_PI * hz / (float)rate;
    float c = cosf(w), sn = sinf(w), b0, b1, b2, a0, a1, a2;
    if (kind == 1) {
        float al = sn / 2.0f;   // Q = 1
        b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A;
    } else {
        float al = sn / 2.0f * sqrtf(2.0f), k = 2 * sqrtf(A) * al;
        if (kind == 0) {
            b0 = A * ((A + 1) - (A - 1) * c + k); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - k);
            a0 = (A + 1) + (A - 1) * c + k; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - k;
        } else {
            b0 = A * ((A + 1) + (A - 1) * c + k); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - k);
            a0 = (A + 1) - (A - 1) * c + k; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - k;
        }
    }
    EqBiquad q = { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    return q;
}

void eq_design(EqState *s, const int gains[EQ_BANDS], int rate) {
    int boost = 0;
    for (int i = 0; i < EQ_BANDS; i++) {
        int g = gains[i] > EQ_MAX_DB ? EQ_MAX_DB : gains[i] < -EQ_MAX_DB ? -EQ_MAX_DB : gains[i];
        if (g > boost) boost = g;
        s->on[i] = g != 0;
        s->bq[i] = design(i == 0 ? 0 : i == EQ_BANDS - 1 ? 2 : 1, (float)EQ_FREQS[i], (float)g, rate);
    }
    s->pre = powf(10.0f, -(float)boost / 20.0f);
    s->active = !eq_is_flat(gains);
}

void eq_process(EqState *s, int16_t *pcm, int frames) {
    if (!s->active) return;
    for (int i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float x = pcm[2 * i + ch] * s->pre;
            for (int b = 0; b < EQ_BANDS; b++) {
                if (!s->on[b]) continue;
                const EqBiquad *q = &s->bq[b];
                float *z = s->z[b][ch];
                float y = q->b0 * x + z[0];   // transposed direct form II
                z[0] = q->b1 * x - q->a1 * y + z[1];
                z[1] = q->b2 * x - q->a2 * y;
                x = y;
            }
            pcm[2 * i + ch] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x);
        }
    }
}
