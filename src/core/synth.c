#include "synth.h"
#include <math.h>

#define TWO_PI 6.28318530718f
#define ATTACK_MS 2.0f

typedef struct { int ms; float amp; } Spec;

static const Spec SPECS[UIS_COUNT] = {
    [UIS_MOVE] = { 18, 0.18f },   [UIS_CONFIRM] = { 45, 0.25f }, [UIS_BACK] = { 45, 0.25f },
    [UIS_TAB] = { 60, 0.20f },    [UIS_EDGE] = { 35, 0.30f },    [UIS_MENU] = { 64, 0.20f },
    [UIS_CHIME] = { 1200, 0.35f }, [UIS_SET_MOVE] = { 22, 0.18f },
};

int synth_length(UiSound s) {
    if (s <= UIS_NONE || s >= UIS_COUNT) return 0;
    return SPECS[s].ms * SYNTH_RATE / 1000;
}

// Linear attack, then a fall to exactly zero at the end (no click at either edge).
static float env_linear(float t, float len, float attack) {
    if (t < attack) return t / attack;
    return (len - t) / (len - attack);
}

// Exponential fall that still ends at zero: e^(-k x) minus its value at the end, rescaled.
static float env_exp(float t, float len, float attack, float k) {
    if (t < attack) return t / attack;
    float x = (t - attack) / (len - attack), end = expf(-k);
    return (expf(-k * x) - end) / (1.0f - end);
}

static uint32_t noise_state;

static float noise(void) {   // LCG, fixed seed per render: the same sound every time
    noise_state = noise_state * 1664525u + 1013904223u;
    return (float)(int32_t)noise_state / 2147483648.0f;
}

// One sample, -1..1, before the amplitude.
static float sample(UiSound s, int i, int n, float *phase) {
    float t = (float)i / SYNTH_RATE, len = (float)n / SYNTH_RATE, a = ATTACK_MS / 1000.0f, f;
    switch (s) {
    case UIS_MOVE:
        *phase += TWO_PI * 2400.0f / SYNTH_RATE;
        return sinf(*phase) * env_exp(t, len, a, 4.0f);
    case UIS_CONFIRM:
    case UIS_BACK:
        f = s == UIS_CONFIRM ? 1200.0f + 600.0f * t / len : 1800.0f - 600.0f * t / len;
        *phase += TWO_PI * f / SYNTH_RATE;
        return sinf(*phase) * env_linear(t, len, a);
    case UIS_TAB:
        *phase += TWO_PI * 700.0f / SYNTH_RATE;
        return (0.7f * sinf(*phase) + 0.3f * noise()) * env_exp(t, len, a, 3.0f);
    case UIS_SET_MOVE:   // a woody tick: 1500 Hz with a quarter of its octave
        *phase += TWO_PI * 1500.0f / SYNTH_RATE;
        return (sinf(*phase) + 0.25f * sinf(2.0f * *phase)) / 1.25f * env_exp(t, len, a, 5.0f);
    case UIS_EDGE:
        *phase += TWO_PI * 220.0f / SYNTH_RATE;
        return sinf(*phase) * env_exp(t, len, a, 5.0f);
    case UIS_MENU: {   // two 12 ms ticks with a 40 ms gap
        float tick = 0.012f, gap = 0.040f, u = t < tick ? t : t - tick - gap;
        if (u < 0.0f || u >= tick) return 0.0f;
        return sinf(TWO_PI * 2000.0f * u) * env_exp(u, tick, a, 4.0f);
    }
    default:
        return 0.0f;
    }
}

// The chime is 1.2 s: sinf/expf per sample took ~2.5 s on the PSP, so its partials are rotating phasors
// and its envelope a running product (a few multiplies per sample).
static void render_chime(int16_t *out, int m, int n) {
    static const float F[4] = { 523.25f, 659.25f, 783.99f, 1046.5f }, G[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
    float c[4], s[4], cd[4], sd[4];
    for (int k = 0; k < 4; k++) {
        c[k] = 1.0f;
        s[k] = 0.0f;
        cd[k] = cosf(TWO_PI * F[k] / SYNTH_RATE);
        sd[k] = sinf(TWO_PI * F[k] / SYNTH_RATE);
    }
    const float k_fall = 4.0f, end = expf(-k_fall);
    int attack = (int)(0.015f * SYNTH_RATE);
    float e = 1.0f, r = expf(-k_fall / (float)(n - attack));
    for (int i = 0; i < m; i++) {
        float v = 0.0f;
        for (int k = 0; k < 4; k++) {
            v += G[k] * s[k];
            float ns = s[k] * cd[k] + c[k] * sd[k], nc = c[k] * cd[k] - s[k] * sd[k];
            s[k] = ns;
            c[k] = nc;
            if ((i & 1023) == 1023) {   // keep the phasor on the unit circle
                float g = 1.0f / sqrtf(s[k] * s[k] + c[k] * c[k]);
                s[k] *= g;
                c[k] *= g;
            }
        }
        float env = i < attack ? (float)i / (float)attack : (e - end) / (1.0f - end);
        if (i >= attack) e *= r;
        if (env < 0.0f) env = 0.0f;
        float x = v / 3.5f * env * SPECS[UIS_CHIME].amp;
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        out[i] = (int16_t)lrintf(x * 32767.0f);
    }
}

int synth_render(UiSound s, int16_t *out, int cap) {
    int n = synth_length(s), m = n < cap ? n : cap;
    if (s == UIS_CHIME) {
        render_chime(out, m, n);
        return m;
    }
    float phase = 0.0f;
    noise_state = 0x2545f491u;
    for (int i = 0; i < m; i++) {
        float v = sample(s, i, n, &phase) * SPECS[s].amp;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (int16_t)lrintf(v * 32767.0f);
    }
    return m;
}
