#ifndef NEMO_M3U_H
#define NEMO_M3U_H

#include <stddef.h>
#include "music.h"

// .m3u / .m3u8 playlists (1.2.1): import from MUSIC and export for the PC.

// Tracks of the library named by a playlist file at m3u_path. Comment lines (#...) are skipped; paths may
// be relative to the playlist, absolute (C:\..., ms0:/...), with / or \. Unmatched relative/absolute paths
// fall back to "folder/file name" at the end of a library path. Text: UTF-8 (BOM ok), else cp1251.
// Returns the tracks found (<= max); *entries = non-comment lines.
int  m3u_parse(const char *m3u_path, const char *text, size_t len, const MusicLib *lib, int *out, int max, int *entries);
// #EXTM3U playlist with paths relative to m3u_path's folder. Bytes written (NUL-terminated), -1 if cap is too small.
int  m3u_format(char *buf, size_t cap, const char *m3u_path, const MusicLib *lib, const int *tracks, int n);
// File name safe on FAT: \ / : * ? " < > | -> '_'; empty -> "playlist".
void m3u_safe_name(char *out, size_t cap, const char *name);
// Path of to_path relative to the folder from_dir, e.g. "../Low Roar/a.mp3".
void m3u_relpath(char *out, size_t cap, const char *from_dir, const char *to_path);

#endif
