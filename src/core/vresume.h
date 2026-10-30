#ifndef NEMO_VRESUME_H
#define NEMO_VRESUME_H

#include <stddef.h>
#include <stdint.h>

// Where each video was left (1.3.3): position, watched to the end, subtitle track and time shift.
// Kept in PSP/NEMO/video_resume.json; the oldest go beyond VRESUME_MAX.
#define VRESUME_MAX 200
#define VR_KEY_MAX 300

typedef struct {
    char key[VR_KEY_MAX];      // "path|size" (a UMD: "umd:<disc id>")
    int32_t pos_ms;
    int done;                  // watched to the last 5 s: starts from the beginning without asking
    int sub_track;             // -1 = subtitles off
    int32_t sub_offset;
    int audio_track;           // UMD Video: the sound track chosen (1.3.4)
    uint32_t seq;              // when last touched
} VrEntry;

typedef struct {
    VrEntry *e;
    int count;
    uint32_t seq;
    int sub_size, sub_bg, sub_pos;   // subtitle look for every video: 0..4 (2), 0 none/1 shadow/2 half/3 black (1), lines up
} VResume;

void vresume_init(VResume *r);
void vresume_free(VResume *r);
void vresume_key(char *out, size_t cap, const char *path, long long size);
const VrEntry *vresume_get(const VResume *r, const char *key);
// Records a stop: under 10 s nothing to resume; within the last 5 s "watched".
void vresume_put(VResume *r, const char *key, int64_t pos_ms, int64_t dur_ms, int sub_track, int32_t sub_offset);
void vresume_set_audio(VResume *r, const char *key, int track);   // kept with the place
int  vresume_ask(const VrEntry *e);            // "Continue from 12:34 or from the start?"
int  vresume_load(VResume *r, const char *json);   // replaces; entries read (broken text: 0)
char *vresume_save(const VResume *r);          // malloc'd JSON

#endif
