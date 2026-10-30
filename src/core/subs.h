#ifndef NEMO_SUBS_H
#define NEMO_SUBS_H

#include <stddef.h>
#include <stdint.h>

// Subtitles for the video player (1.3.3): .srt and .ass/.ssa (text only), UTF-8 / cp1251 / cp866 by
// detection, tags removed; the text for a moment; word wrap; files next to a video.
#define SUBS_MAX_LINES 3

typedef struct { int32_t start_ms, end_ms; char *text; } SubCue;   // text: UTF-8, lines split by '\n'
typedef struct { SubCue *cues; int count, cap; } SubList;

int  subs_parse(SubList *l, const char *data, size_t n);   // replaces the list; cues by start time; count
int  subs_add(SubList *l, int32_t start_ms, int32_t end_ms, const char *utf8);   // (MP4 text track) 0 ok
void subs_sort(SubList *l);
void subs_free(SubList *l);
// The text shown at video time ms; offset_ms > 0 shows the subtitles earlier. "" when none.
const char *subs_at(const SubList *l, int64_t ms, int32_t offset_ms, char *out, size_t cap);
// Splits text into at most SUBS_MAX_LINES lines no wider than max_w (by width()); returns the count.
int  subs_wrap(const char *text, int max_w, int (*width)(void *ctx, const char *s), void *ctx,
               char lines[SUBS_MAX_LINES][256]);
// Subtitle files beside a video: <stem>.srt / <stem>.*.srt / .ass / .ssa, any case; the exact name first.
int  subs_find(const char *dir, const char *video_name, char (*names)[256], int max);

#endif
