#ifndef NEMO_UMDMUSIC_H
#define NEMO_UMDMUSIC_H

#include "music.h"

// UMD Music discs (1.3.5): the disc's streams (UMD_AUDIO/STREAM/*.MPS) as one album at the end of the library,
// "UMD: <title>", tracks "Трек N" in the order of the file names. Taken out again when the disc leaves.
typedef struct {
    int first_track, album;   // -1 when not attached
    int prev_tracks, prev_albums;
} UmdMusicRef;

// files: bare names inside dir; title_fmt takes the track number. 0 attached.
int  umdmusic_attach_ref(MusicLib *l, const char *title, const char *title_fmt, const char *dir, const char *const *files,
                         int n, UmdMusicRef *r);
void umdmusic_detach(MusicLib *l, UmdMusicRef *r);
int  umdmusic_is_track(const Track *t);     // a track of a UMD disc
int  umdmusic_is_stream(const char *name);  // *.MPS

#endif
