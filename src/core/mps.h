#ifndef NEMO_MPS_H
#define NEMO_MPS_H

#include <stdint.h>

// UMD Video streams (UMD_VIDEO/STREAM/*.MPS: MPEG-PS packs of 2048 bytes, H.264 in a video stream 0xE0-0xEF) — the
// presentation times in the PES headers (90 kHz), for the length of a film and for seeking (1.3.3).
int mps_first_pts(const uint8_t *buf, int n, int64_t *pts);   // the first video PTS in buf; 0 found
int mps_last_pts(const uint8_t *buf, int n, int64_t *pts);    // the last one; 0 found
int64_t mps_duration_ms(int64_t first, int64_t last);
// The sound packets' times (1.3.5, UMD Music: no picture): private stream 1.
int mps_audio_first_pts(const uint8_t *buf, int n, int64_t *pts);
int mps_audio_last_pts(const uint8_t *buf, int n, int64_t *pts);
// The sound tracks in buf (1.3.4): bit n set when private stream 1 carries substream n (0..15).
int mps_audio_tracks(const uint8_t *buf, int n);

#endif
