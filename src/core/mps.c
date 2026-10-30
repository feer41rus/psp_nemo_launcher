#include "mps.h"

// The PTS of a video PES header at p (p[0..3] = 00 00 01 Ex), -1 when it has none or is cut off.
static int64_t pes_pts(const uint8_t *p, int left) {
    if (left < 14 || (p[6] & 0xC0) != 0x80 || !(p[7] & 0x80)) return -1;   // MPEG-2 PES, PTS present
    const uint8_t *t = p + 9;
    if (9 + p[8] > left) return -1;
    return ((int64_t)(t[0] & 0x0E) << 29) | ((int64_t)t[1] << 22) | ((int64_t)(t[2] & 0xFE) << 14) |
           ((int64_t)t[3] << 7) | (t[4] >> 1);
}

static int scan(const uint8_t *buf, int n, int64_t *pts, int want_last, int audio) {
    int found = 0;
    for (int i = 0; i + 14 <= n; i++) {
        if (buf[i] || buf[i + 1] || buf[i + 2] != 1) continue;
        if (audio ? buf[i + 3] != 0xBD : (buf[i + 3] & 0xF0) != 0xE0) continue;   // private stream 1 / video E0-EF
        int64_t v = pes_pts(buf + i, n - i);
        if (v < 0) continue;
        *pts = v;
        found = 1;
        if (!want_last) break;
    }
    return found ? 0 : -1;
}

int mps_first_pts(const uint8_t *buf, int n, int64_t *pts) { return scan(buf, n, pts, 0, 0); }
int mps_last_pts(const uint8_t *buf, int n, int64_t *pts) { return scan(buf, n, pts, 1, 0); }
int mps_audio_first_pts(const uint8_t *buf, int n, int64_t *pts) { return scan(buf, n, pts, 0, 1); }
int mps_audio_last_pts(const uint8_t *buf, int n, int64_t *pts) { return scan(buf, n, pts, 1, 1); }

int64_t mps_duration_ms(int64_t first, int64_t last) { return last > first ? (last - first) / 90 : 0; }

int mps_audio_tracks(const uint8_t *buf, int n) {
    int mask = 0;
    for (int i = 0; i + 9 <= n; i++) {
        if (buf[i] || buf[i + 1] || buf[i + 2] != 1 || buf[i + 3] != 0xBD) continue;
        if ((buf[i + 6] & 0xC0) != 0x80) continue;   // MPEG-2 PES
        int pay = i + 9 + buf[i + 8];                // after the PES header: the substream number
        if (pay >= n) continue;
        if (buf[pay] < 16) mask |= 1 << buf[pay];
    }
    return mask;
}
