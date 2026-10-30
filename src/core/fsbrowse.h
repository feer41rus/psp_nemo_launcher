#ifndef NEMO_FSBROWSE_H
#define NEMO_FSBROWSE_H

#include <stddef.h>

// File manager (1.3.2): folder listings and path rules. Paths look like "ms0:/ISO/a.iso"; a root is "ms0:/".
#define FS_LIST_MAX 2000
#define FS_PATH_MAX 512

typedef struct {
    char name[256];         // as the card lists it (Shift-JIS on the PSP): for paths; fs_display for the screen
    int is_dir;
    long long size;
    long mtime;
    int marked;
} FsEntry;

typedef struct {
    FsEntry *items;
    int count, truncated;   // truncated: the folder holds more than FS_LIST_MAX
} FsList;

int  fs_list(const char *dir, FsList *out);   // folders first, then names ignoring case; 0 ok, -1 unreadable
void fs_list_free(FsList *l);
void fs_display(const char *name, char *out, size_t cap);   // a card name (Shift-JIS) as UTF-8 for the screen

int  fs_join(char *out, size_t cap, const char *dir, const char *name);   // -1: does not fit (never cut)
int  fs_parent(char *path);                   // one level up in place; 0 when already a root
int  fs_is_root(const char *path);
const char *fs_name(const char *path);        // the last part ("" for a root)
int  fs_same_device(const char *a, const char *b);
int  fs_inside(const char *dir, const char *path);   // path is dir or below it

// Changing these may stop ARK or Nemo from starting: ARK's save folder, SEPLUGINS, PSP/NEMO, and the
// folders that hold them. Asked twice, never refused.
int  fs_protected(const char *path);
int  fs_is_textfile(const char *name);        // opens in the editor on ✕
int  fs_game_path(const char *path);          // a change here means the game library must be rescanned

#endif
