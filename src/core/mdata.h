#ifndef NEMO_MDATA_H
#define NEMO_MDATA_H

#include <stdint.h>
#include "music.h"
#include "queue.h"
#include "eq.h"

#define MD_MAX_PLAYLISTS 32
#define MD_RECENT 50
#define MD_ID_LEN 256

typedef struct {
    char name[64];
    int count, cap;
    char (*ids)[MD_ID_LEN];
} MPlaylist;

// Player data in music.json: favorites, recent, user playlists and the saved player state.
typedef struct {
    MPlaylist favorites, recent;
    MPlaylist lists[MD_MAX_PLAYLISTS];
    int list_count;
    MPlaylist queue;          // saved play order (ids)
    int queue_pos;
    uint32_t position_ms;
    int shuffle, repeat;
    int view;                 // library view: 0 albums, 1 artists, 2 folders (1.2.1), 3 all tracks (1.2.3)
    int eq_on, eq_preset;     // equalizer switch and EqPreset (1.2.1)
    int eq_custom[EQ_BANDS];  // "Custom" preset gains, dB
    MPlaylist m3u_imported;   // lowercase paths of .m3u files already turned into playlists (or written by Nemo)
} MusicData;

void mdata_init(MusicData *d);
void mdata_free(MusicData *d);
int  mdata_load(MusicData *d, const char *path);         // 0 ok/missing, -1 corrupt (reset)
int  mdata_save(const MusicData *d, const char *path);   // crash-safe
char *mdata_render(const MusicData *d);                  // the JSON mdata_save writes (malloc, caller frees)

int  mplaylist_add(MPlaylist *p, const char *id);        // idempotent; -1 on OOM
void mplaylist_remove(MPlaylist *p, const char *id);
int  mplaylist_has(const MPlaylist *p, const char *id);

void mdata_touch_recent(MusicData *d, const char *id);   // to the front, dedupe, cap MD_RECENT
int  mdata_playlist_create(MusicData *d, const char *name);          // index, -1 empty/dup/full
int  mdata_playlist_rename(MusicData *d, int idx, const char *name);  // 0, -1 empty/dup
void mdata_playlist_delete(MusicData *d, int idx);

void mdata_set_queue(MusicData *d, const Queue *q, const MusicLib *lib, uint32_t position_ms);
// Rebuild a queue from saved ids; tracks missing from lib are skipped. If the current track is
// gone, the next existing one becomes current and the position resets. 0 ok, -1 nothing left.
int  mdata_restore_queue(const MusicData *d, const MusicLib *lib, Queue *q, uint32_t *position_ms);

#endif
