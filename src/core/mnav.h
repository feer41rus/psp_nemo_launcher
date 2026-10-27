#ifndef NEMO_MNAV_H
#define NEMO_MNAV_H

#include "input.h"

typedef enum { MS_LIBRARY, MS_NOW, MS_MENU, MS_QUEUE, MS_PICK, MS_OSK, MS_CONFIRM, MS_LISTEN, MS_EQ } MusicScreen;

// Sizes of what the Music tab shows; filled by the UI before every mnav_handle call.
typedef struct {
    int side_count;     // albums + playlists in the left column
    int track_count;    // tracks of the selected album/playlist
    int menu_count;     // rows of the open triangle menu
    int queue_count;
    int pick_count;     // playlists in the "add to playlist" picker
    int folder_is_root; // folders view: the current folder is MUSIC (1.2.1)
    int eq_rows;        // equalizer screen rows (on, preset, bands)
} MusicModel;

typedef struct {
    MusicScreen kind;
    int sel;
} MusicNavScreen;

#define MNAV_DEPTH 6

// Now Playing focus: the progress bar or one of the transport buttons.
typedef enum { NP_PROGRESS, NP_SHUFFLE, NP_PREV, NP_PLAY, NP_NEXT, NP_REPEAT } NowFocus;

typedef struct {
    MusicNavScreen stack[MNAV_DEPTH];   // stack[0] is always MS_LIBRARY
    int depth;
    int column;                         // library: 0 = side column, 1 = track list
    int side_sel, track_sel;
    NowFocus np_focus;                  // cursor on Now Playing
    NowFocus np_last_button;            // button to return to from the progress bar
    int view;                           // library view: 0 albums, 1 artists, 2 folders (1.2.1), 3 all tracks (1.2.3)
    int artist_open;                    // artists view: -1 = overview on the right, else the opened row
    int folder;                         // folders view: current folder (FolderTree index)
} MusicNav;

typedef enum {
    MC_NONE, MC_PLAY_TRACK, MC_OPEN_NOW, MC_TOGGLE_PAUSE, MC_SEEK, MC_SHUFFLE, MC_REPEAT,
    MC_OPEN_MENU, MC_MENU_PICK, MC_QUEUE_JUMP, MC_QUEUE_REMOVE, MC_PICK, MC_LISTEN,
    MC_SIDE_CHANGED, MC_CONFIRM_YES, MC_NP_BUTTON,
    MC_VIEW_NEXT, MC_ARTIST_OPEN, MC_ARTIST_BACK, MC_FOLDER_ENTER, MC_FOLDER_UP, MC_EQ_CHANGE, MC_EQ_RESET
} MusicCmdType;

typedef struct {
    MusicCmdType type;
    int index;
    int dir;
} MusicCmd;

void mnav_init(MusicNav *n);
MusicCmd mnav_handle(MusicNav *n, Action a, const MusicModel *m);
void mnav_push(MusicNav *n, MusicScreen s);
void mnav_pop(MusicNav *n);
MusicScreen mnav_top(const MusicNav *n);
int mnav_sel(const MusicNav *n);        // cursor of the top screen (lists/menus)
void mnav_set_view(MusicNav *n, int view);            // also resets cursors, artist_open = -1, folder = 0
void mnav_clamp_folder(MusicNav *n, int folder_count); // after a rescan: an index past the tree -> root
// First row to draw of a list of `visible` rows: the eased scroll position, moved just enough that the
// selected row is always on screen (the animation may lag behind a held arrow).
int mnav_first_visible(float scroll, int sel, int visible);

#endif
