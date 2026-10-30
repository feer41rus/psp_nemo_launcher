#ifndef NEMO_MP4_H
#define NEMO_MP4_H

#include <stdint.h>

// MP4 (ISO BMFF) reading for the video player (1.3.3): tracks, codec setup, and where each sample is
// and when it shows. The file is read through a callback; only the moov box is kept in memory.
typedef int (*Mp4ReadFn)(void *ctx, long long off, void *buf, int n);   // bytes read, -1 error

typedef enum { MT_VIDEO, MT_AUDIO, MT_TEXT, MT_OTHER } Mp4Kind;

#define MP4_MAX_TRACKS 8

typedef struct {
    Mp4Kind kind;
    char codec[5];                 // "avc1", "mp4a", "tx3g"...
    int width, height;             // video
    int sample_rate, channels, bits;   // audio
    const uint8_t *config;         // video: avcC; audio: AudioSpecificConfig (inside Mp4.moov)
    int config_len;
    char lang[4];                  // "rus", "eng", "und"
    uint32_t timescale;
    int64_t duration;              // in timescale
    int64_t media_start;           // edit list: the first shown time, in timescale
    int n_samples;
    // sample tables (pointers into Mp4.moov, big-endian as stored)
    const uint8_t *stts, *ctts, *stsc, *stsz, *stco, *stss;
    uint32_t n_stts, n_ctts, n_stsc, n_stco, n_stss, fixed_size;
    int co64;
    int64_t empty_ms;              // edit list: an empty start (shown later by this much)
    // lookup cursors: playing reads samples in order, so each lookup continues from the last one
    uint32_t stts_run; int stts_first; int64_t stts_dts;
    uint32_t ctts_run; int ctts_first;
    uint32_t stsc_run; int stsc_first;
} Mp4Track;

typedef struct {
    uint8_t *moov;
    int n_tracks;
    Mp4Track tracks[MP4_MAX_TRACKS];
    uint32_t timescale;            // movie
    int64_t duration;              // movie timescale
} Mp4;

typedef struct {
    long long offset;
    int size;
    int key;
    int64_t pts_ms;                // display time
    int64_t dts_ms;                // decode time
    int dur_ms;
} Mp4Sample;

int  mp4_open(Mp4 *m, Mp4ReadFn read, void *ctx, long long file_size);   // 0 ok, -1 not an MP4 / broken
void mp4_close(Mp4 *m);
int  mp4_track(const Mp4 *m, Mp4Kind kind);         // first track of that kind, -1 none
int64_t mp4_duration_ms(const Mp4 *m);
int  mp4_sample(Mp4 *m, int track, int i, Mp4Sample *out);         // 0 ok, -1 past the end
int  mp4_key_before(const Mp4 *m, int track, int64_t ms);         // sync sample at or before ms
// Samples i.. that lie one after another in the file (one chunk), at most max: their count, the offset of
// the first and the bytes of them all. For PCM sound stored one frame per sample (QuickTime 'sowt').
int  mp4_run(Mp4 *m, int track, int i, int max, long long *off, int *bytes);
int  mp4_sample_at(Mp4 *m, int track, int64_t ms);                // sample whose decode time covers ms
void mp4_text(const void *sample, int n, char *out, int cap);      // tx3g sample -> UTF-8 text

#endif
