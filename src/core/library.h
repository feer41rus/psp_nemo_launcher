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
    int show_photo;    // Photos tab (1.3.1, default on)
    int replace_xmb;   // Nemo instead of XMB (1.1.11, default off)
    int dbg_ram, dbg_cpu, dbg_fps;   // Settings › System › Debug: debug box rows (1.2.0, default off)
    int log_keep;      // runs of nemo.log to keep: 0 (no log), 2, 5, 10, 20 (1.2.0, default 2)
    int verbose_log;   // Settings › System › Debug › Detailed log: everything, for beta testing (1.3.3, default off)
    int splash_hello;  // welcome splash at power-on (1.2.3, default on)
    int splash_ncl;    // NCL thank-you splash: shown once at the next power-on, then off (1.2.3, default off)
    int ui_sounds;     // interface clicks (1.2.3, default on)
    int control_center;   // Control Center plugin in games (1.2.4, default on)
    int low_space;     // "Low on space" badge on Home (1.3.10, default on)
    int bat_pct;       // battery percentage next to the icon at the top right (1.3.9, default on)
    int lib_bg;        // Library background, a LibBg: Nemo's pictures or LIBBG_OWN from the Gallery (1.3.8, default 0)
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
// settings.log_keep straight from the file, before the log is opened (LOG_KEEP_DEFAULT when absent/bad).
int library_peek_log_keep(const char *path);
int library_peek_verbose(const char *path);   // settings.verbose_log the same way (0 when absent)

int library_folder_create(Library *l, const char *name);           // index, -1 empty/dup/full
int library_folder_rename(Library *l, int idx, const char *name);  // 0, -1 empty/dup
void library_folder_delete(Library *l, int idx);
int library_folder_add(Library *l, int idx, const char *game_id);  // 0 (idempotent), -1 full
void library_folder_remove(Library *l, int idx, const char *game_id);
int library_folder_has(const Library *l, int idx, const char *game_id);

void library_touch_recent(Library *l, const char *game_id, int disc);
int library_last_disc(const Library *l, const char *game_id);      // 0 = unknown

#endif
