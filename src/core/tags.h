#ifndef NEMO_TAGS_H
#define NEMO_TAGS_H

#include <stddef.h>
#include <stdint.h>

typedef enum { AF_MP3, AF_FLAC, AF_OGG, AF_WAV, AF_UNKNOWN } AudioFormat;

AudioFormat audio_format_from_name(const char *name);   // by extension, case-insensitive

typedef struct {
    char title[128], artist[96], album[128], album_artist[96];
    int year, track_no, disc_no;                        // 0 = unknown
    uint32_t cover_offset, cover_size;                  // embedded picture bytes, 0 = none
} TagInfo;

// Read ID3v1/v2.2-2.4 (MP3), Vorbis comments + PICTURE (FLAC), Vorbis comments (OGG).
// Returns 0 when the file could be opened (fields may be empty), -1 otherwise.
int tags_read(const char *path, AudioFormat fmt, TagInfo *out);

// Decode ID3 text (enc 0 = Latin-1 or cp1251 by detection, 1 = UTF-16 with BOM,
// 2 = UTF-16BE, 3 = UTF-8) into NUL-terminated UTF-8. Returns the output length.
int text_decode(const uint8_t *s, size_t n, int enc, char *out, size_t cap);

// Heuristic: 8-bit text that looks like Russian in cp1251 (two high letters in a row).
int cp1251_likely(const uint8_t *s, size_t n);

#endif
