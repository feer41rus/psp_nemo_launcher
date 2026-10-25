#ifndef NEMO_LIBRARY_H
#define NEMO_LIBRARY_H

#define LIB_MAX_FOLDERS   32
#define LIB_MAX_IN_FOLDER 128
#define LIB_MAX_RECENT    12
#define LIB_MAX_DISCS     64
#define LIB_ID_LEN        160

typedef struct {
    char name[64];
    int count;
    char ids[LIB_MAX_IN_FOLDER][LIB_ID_LEN];
} LibFolder;

typedef struct {
    char id[LIB_ID_LEN];
    int disc;
} LibDisc;

// Settings › System › Nemo Launcher (1.1.7).
typedef struct {
    int xmb_exit;      // "Exit to XMB" in the HOME power window (default off)
    int ncl_logo;      // NCL logo on Home (default on)
    int site;          // nemoleague.ru in "About Nemo" (default on)
    int show_music;    // Music tab; off = music stopped and unloaded (default on)
    int replace_xmb;   // Nemo instead of XMB (1.1.11, default off)
} NemoSettings;

void library_settings_defaults(NemoSettings *s);

// Nemo's own data: user folders, recently played games, last disc, UI language.
typedef struct {
    LibFolder folders[LIB_MAX_FOLDERS];
    int folder_count;
    char recent[LIB_MAX_RECENT][LIB_ID_LEN];   // [0] = most recent
    int recent_count;
    LibDisc last_disc[LIB_MAX_DISCS];
    int last_disc_count;
    char language[3];                          // "ru" | "en" (unused since 1.1.5: system language)
    NemoSettings settings;
} Library;

void library_init(Library *l);
int library_load(Library *l, const char *path);        // 0 ok or missing file; -1 corrupt (reset)
int library_save(const Library *l, const char *path);  // tmp + rename; creates parent dirs

int library_folder_create(Library *l, const char *name);           // index, -1 empty/dup/full
int library_folder_rename(Library *l, int idx, const char *name);  // 0, -1 empty/dup
void library_folder_delete(Library *l, int idx);
int library_folder_add(Library *l, int idx, const char *game_id);  // 0 (idempotent), -1 full
void library_folder_remove(Library *l, int idx, const char *game_id);
int library_folder_has(const Library *l, int idx, const char *game_id);

void library_touch_recent(Library *l, const char *game_id, int disc);
int library_last_disc(const Library *l, const char *game_id);      // 0 = unknown

#endif
