#ifndef NEMO_MUSIC_H
#define NEMO_MUSIC_H

#include <stdint.h>
#include "scanner.h"   // ScanStats
#include "tags.h"

typedef struct {
    char path[256], id[256];            // id = lowercase path
    char title[128], artist[96], album[128], album_artist[96];
    int year, track_no, disc_no;
    AudioFormat format;
    uint32_t cover_offset, cover_size;  // embedded picture, 0 = none
} Track;

typedef struct {
    char title[128], artist[96];
    int year;
    int first, count;                   // tracks[first .. first + count - 1]
    char folder[256];                   // directory of the first track (folder covers)
} Album;

typedef struct {
    Track *tracks;
    int track_count, track_cap;
    Album *albums;
    int album_count, album_cap;
} MusicLib;

void musiclib_init(MusicLib *l);
void musiclib_free(MusicLib *l);

// Scan root (e.g. "ms0:/MUSIC") up to 4 folder levels deep; reuse/refresh cache_path (may be NULL).
// Tracks are grouped into albums (album tag + album artist, else folder name) and sorted.
// Returns the number of tracks.
int music_scan(const char *root, const char *cache_path, MusicLib *lib, ScanStats *stats);

int musiclib_find(const MusicLib *l, const char *id);   // track index or -1
void music_title_from_filename(const char *path, char *out, size_t cap);

// Cover image next to the tracks: cover.* > folder.* > front.* (jpg/jpeg/png, any case).
// 0 and the full path in out when found, -1 otherwise.
int music_folder_cover(const char *folder, char *out, size_t cap);

#endif
