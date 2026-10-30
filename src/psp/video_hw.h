#ifndef NEMO_VIDEO_HW_H
#define NEMO_VIDEO_HW_H

#include "subs.h"
#include <stdint.h>

// Video playback (1.3.3): camera AVI (Motion JPEG 4:2:0 through sceJpeg, PCM sound) and MP4 (H.264 Baseline /
// Main up to 480x272 without B-frame pyramids, AAC). Pictures are decoded on a thread; the sound thread is
// the clock.
typedef enum { VO_OK, VO_NO_FILE, VO_MP4_SOON, VO_UNSUPPORTED, VO_NO_MEMORY, VO_MOV_MJPEG } VideoOpen;   // the last: internal

typedef struct {
    char container[8];      // "AVI", "MP4", "MOV", "MKV"...
    char vcodec[8];         // "MJPG", "avc1"
    int width, height;
    int fps_x100;
    char audio[24];         // "PCM 22050 Hz mono", "" none
    int64_t duration_ms;
    long long size;
} VideoInfo;

VideoOpen video_open(const char *path, VideoInfo *info, char *why, int why_cap);
void video_close(void);
void video_play(int64_t from_ms);
void video_pause(int on);
int  video_paused(void);
void video_seek(int64_t ms);
int64_t video_pos_ms(void);
int  video_ended(void);
// A new picture since the last call: 1 and its pixels (RGBA, 480 per row), else 0.
int  video_take_frame(const uint32_t **px, int *w, int *h, int *stride);
void video_frame_done(void);   // the picture taken was copied: its buffer may be decoded into again
void video_stats(int *decoded, int *dropped, int *worst_ms);
int  video_sound_late(void);           // sound buffers handed over after the previous one ran out
int  video_text_track(SubList *out);   // MP4 subtitles inside the file (tx3g): cues added; count
const char *video_text_lang(void);     // their language ("rus", "eng", "und")
// UMD Video (1.3.4): the streams of the disc (film, trailers, extras) and the sound tracks of the one playing.
typedef struct { char file[32]; long long size; int64_t duration_ms; } VideoStream;
int  video_umd_streams(VideoStream *out, int max);   // by name, with their lengths (reads the disc: slow)
int  video_audio_tracks(int *current);               // bit mask of the tracks (0: not a disc), the one playing
void video_set_audio_track(int ch);                  // rebuilt at the same place with that track

#endif
