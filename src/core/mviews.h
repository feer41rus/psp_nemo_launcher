#ifndef NEMO_MVIEWS_H
#define NEMO_MVIEWS_H

#include "music.h"

// Indexes behind the Music library views (1.2.1). Built once after a scan from MusicLib; they hold
// album/track numbers and pointers into the library, never copies of track strings.

// ---- Artists: albums grouped by Album.artist (album artist, else track artist), case-insensitive ----
typedef struct {
    const char *name;    // points into MusicLib (Album.artist); "" = unknown artist
    int album_first;     // into ArtistIndex.albums
    int album_count, track_count;
} MArtist;

typedef struct {
    MArtist *artists;    // sorted by name (utf8_casecmp), unknown artist last
    int count;
    int *albums;         // album indices, per artist newest year first
} ArtistIndex;

int  artists_build(ArtistIndex *ix, const MusicLib *lib);   // 0, -1 out of memory (index left empty)
void artists_free(ArtistIndex *ix);
// Every track of the artist: albums newest first, each in its own order. Returns the count (<= max).
int  artists_tracks(const ArtistIndex *ix, const MusicLib *lib, int artist, int *out, int max);

// ---- Folders under the MUSIC root, built from track paths (folders with no music below are absent) ----
typedef struct {
    char path[256];      // no trailing '/'; raw (Shift-JIS) as listed by the PSP
    char name[128];      // last component in UTF-8, for the screen
    int parent;          // -1 for the root
    int direct, total;   // tracks right here / here and below
} MFolder;

typedef struct {
    MFolder *folders;    // folders[0] = root
    int count;
} FolderTree;

int  folders_build(FolderTree *t, const MusicLib *lib, const char *root);   // 0, -1 out of memory
void folders_free(FolderTree *t);
const char *folders_name(const FolderTree *t, int f);                        // UTF-8 last component; root -> "MUSIC"
int  folders_children(const FolderTree *t, int f, int *out, int max);        // sorted by name (utf8_casecmp)
// Tracks right in folder f (recursive: also below), sorted by path. Returns the count (<= max).
int  folders_tracks(const FolderTree *t, const MusicLib *lib, int f, int recursive, int *out, int max);
int  folders_find(const FolderTree *t, const char *path);                    // index or -1

// "All tracks" view (1.2.3): every track of the library, by title (case-insensitive), then artist.
// Writes at most cap indices to out; returns how many.
int alltracks_build(const MusicLib *lib, int *out, int cap);

#endif
