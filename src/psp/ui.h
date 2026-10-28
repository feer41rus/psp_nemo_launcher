#ifndef NEMO_UI_H
#define NEMO_UI_H

#include <stdint.h>
#include "game.h"
#include "i18n.h"
#include "input.h"
#include "library.h"
#include "nav.h"
#include "osk.h"
#include "shelves.h"
#include "mdata.h"
#include "mnav.h"
#include "mviews.h"
#include "music.h"
#include "notekey.h"
#include "selectkey.h"
#include "perfstat.h"
#include "uisnd.h"
#include "queue.h"
#include "sleeptimer.h"
#include "netnav.h"
#include "net.h"
#include "ftpsrv.h"
#include "idle.h"
#include "homekey.h"
#include "powermenu.h"
#include "setmenu.h"
#include "datetime.h"
#include "powersave.h"

#define TOP_BAR_H 20
#define HINT_BAR_H 20
#define CONTENT_Y TOP_BAR_H
#define CONTENT_H (SCREEN_H - TOP_BAR_H - HINT_BAR_H)

typedef enum { SET_CONFIRM, SET_LANGUAGE, SET_OPEN_ARK, SET_EXIT_XMB, SET_COUNT } SettingRow;

typedef enum { MI_TO_FOLDER, MI_REMOVE_FROM_FOLDER, MI_DETAILS, MI_RENAME_FOLDER, MI_DELETE_FOLDER } MenuItem;
typedef enum { OSK_FOR_NEW_FOLDER, OSK_FOR_RENAME, OSK_FOR_NEW_PLAYLIST, OSK_FOR_RENAME_PLAYLIST, OSK_FOR_WIFI_PASSWORD, OSK_FOR_NICKNAME } OskPurpose;
// Network tab screens on top of the section column / panel.
typedef enum { NETS_MAIN, NETS_OSK, NETS_MENU, NETS_CONFIRM_FORGET, NETS_CONFIRM_RESCAN, NETS_USB, NETS_FTP } NetScreen;
#define NET_ROWS_MAX (NET_MAX_APS + NET_MAX_PROFILES)

typedef enum {
    MM_TO_QUEUE, MM_TO_PLAYLIST, MM_FAVORITE, MM_QUEUE, MM_SLEEP, MM_LISTEN,
    MM_RENAME_PLAYLIST, MM_DELETE_PLAYLIST, MM_EQ, MM_PLAY_FOLDER, MM_PLAY_FOLDER_ALL, MM_SHUFFLE_FOLDER_ALL, MM_EXPORT_M3U
} MusicMenuItem;
typedef enum { SIDE_ALBUM, SIDE_FAVORITES, SIDE_RECENT, SIDE_PLAYLIST, SIDE_ARTIST, SIDE_FOLDER_UP, SIDE_FOLDER, SIDE_ALL } SideKind;
#define MUSIC_FOLDER_KIDS 256
#define MENU_MAX 6

typedef struct {
    GameList games;
    Library *lib;
    Nav nav;
    NavModel model;
    Shelf home[NAV_MAX_SHELVES];
    int home_count, continue_game;
    Shelf library[NAV_MAX_SHELVES];
    int library_count;
    int confirm_cross;
    int registry_ok;
    Osk osk;
    OskPurpose osk_purpose;
    int osk_folder;          // folder being renamed
    MenuItem menu[MENU_MAX]; // action menu (triangle) for menu_game / menu_folder
    int menu_count;
    int menu_game;
    int menu_folder;         // folder shelf the menu was opened on, -1 otherwise
    int pick_game;           // game the folder picker adds/removes
    char toast[160];
    uint32_t toast_until;
    uint32_t now_ms;
    // Smooth scrolling (current values eased towards targets every frame).
    float scroll_y[TAB_COUNT];
    float scroll_x[TAB_COUNT][NAV_MAX_SHELVES];
    uint32_t card_opened_ms;
    int debug;               // SELECT+TRIANGLE: debug line + debug box
    int fps;
    PerfStat perf;           // debug box: frame load (1.2.0)
    uint32_t ram_free, ram_total;
    int cpu_mhz;
    int screenshot;        // capture the next frame
    // Music player
    MusicLib mlib;
    MusicData *mdata;
    Queue queue;
    NoteKey note;
    MusicNav mnav;
    MusicModel mmodel;
    SleepTimer sleep;
    uint32_t music_resume_ms;   // position to start from when the restored track is first played
    uint32_t music_saved_ms;    // last periodic state save
    uint32_t hprm_prev;
    MusicMenuItem mmenu[8];     // triangle menu of the Music tab
    int mmenu_count, mmenu_ctx; // ctx: 0 side entry, 1 track, 2 now playing
    int mmenu_track, mmenu_side;
    int mpick_track;            // track the playlist picker adds
    int mconfirm;               // what MS_CONFIRM asks: 0 delete playlist, 1 reset equalizer (1.2.1)
    int music_scan_state;       // 0 idle, 1 scanning the MUSIC folder on a thread, 2 last scan found nothing
    int music_scan_seen;        // tracks seen so far by the running scan
    int listening;              // listening mode active (warning screen, then screen off)
    uint32_t listen_started_ms;
    float music_scroll[2];      // library columns
    ArtistIndex artists;        // artists view (1.2.1)
    FolderTree folders;         // folders view: MUSIC tree from track paths
    int *alltracks, alltrack_count;   // all-tracks view (1.2.3): every track, by title
    int fkids[MUSIC_FOLDER_KIDS], fkid_count;   // subfolders of mnav.folder, sorted
    // Network tab
    int has_wifi;               // model has WLAN hardware (E1000 does not)
    NetNav netnav;
    NetScreen net_screen;
    NetSnapshot net;            // refreshed every frame while Wi-Fi is started
    NetRow net_rows[NET_ROWS_MAX];
    int net_row_count;
    NetAp net_target;           // network the password keyboard / retry is for
    int net_target_pw;          // last attempt used a typed password (retry reopens the keyboard)
    int net_menu_sel, net_confirm_sel, net_slot;
    int wpa2_on;
    FtpStatus ftp;              // refreshed every frame
    int media_suspended;        // FTP/USB: music stopped, caches freed (author's request)
    Idle idle;                  // USB / FTP screens: screen off after a minute without a press
    HomeKey home_key;           // HOME: tap on release, hold 3 s = power window
    PowerMenu power;            // power window over the current screen
    // Settings (ui_settings.c)
    SetMenu setmenu;
    int date_format, time_h12, tz_minutes;   // registry /CONFIG/DATE
    int dt_open, dt_field;      // date/time editor
    DateTime dt_edit;
    int clock_prompt, clock_prompt_sel;      // "clock was reset, set it now?"
    int nick_open;              // nickname keyboard
    char nickname[80];          // /CONFIG/SYSTEM/owner_name
    int backlight_s, sleep_s;   // /CONFIG/SYSTEM/POWER_SAVING (XMB values, seconds)
    PowerSave psave;
    char *ark_text;             // SETTINGS.TXT cache for Settings › ARK / Overclock (NULL = unreadable)
    int oc_warn;                // overclock warning shown on entering the section
    int pick_open, pick_sel, pick_top;   // drop-down list: clocks (Settings › ARK › Overclock), time zones
    SetId pick_id;
    int music_off;              // Settings › Nemo › Music off: stopped and unloaded
    int reset_open, reset_sel, reset_confirm;   // Settings › System › Reset
    int reset_marks[4];         // console, ARK, Nemo settings, Nemo data
    struct PluginsScreen *plug; // Settings › Plugin manager (ui_plugins.c), NULL = closed
    struct TextPage *page;      // About › Creators / Testers / Thanks (textpage.c)
    struct SavesScreen *saves;  // saved data: from the game card or Settings (ui_saves.c)
    struct BatteryScreen *battery;   // battery screens (ui_battery.c, 1.2.2)
    int gm_open, gm_sel, gm_mode, gm_pick, gm_yes, gm_with_saves, gm_list;   // game card menu (gamemenu.c)
    int rx_stage, rx_yes;       // "ARK replaced Nemo, restore?" (selfguard.c)
    float net_scroll;
    int volume;            // last known main volume (0..30), -1 unknown
    uint32_t volume_until; // volume indicator visible until this time
    UiSndState snd;        // interface sounds (1.2.3)
    int splash_active;     // start-up splash on screen: everything else stays quiet
    int card_dlc;          // the open card's game has DLC installed (1.2.5)
    long long card_bytes;  // its size on the card, -1 unknown (UMD)
    uint32_t umd_notice_until;   // "UMD inserted" notice on screen until (START plays the disc)
    char umd_notice[160];
} App;

typedef struct {
    Action act;
    StrId label;
} Hint;

void ui_toast(App *app, const char *msg);

void ui_chrome_draw(const App *app);
void ui_hints_draw(const App *app, const Hint *hints, int n);
void ui_toast_draw(const App *app);
void ui_volume_draw(const App *app);

void ui_stub_draw(const App *app, StrId title, StrId desc);

// Home / Library shelves. Returns nothing; focus comes from app->nav.
void ui_shelves_draw(App *app, Tab tab);
// Game index under the focus of a shelf tab (continue banner included), -1 if none.
int ui_focused_game(const App *app, Tab tab);
const char *ui_kind_label(const Game *g);
uint32_t ui_game_color(const Game *g, int bottom);

void ui_card_draw(App *app);

// Music player (musicctl.c / ui_music.c)
void music_init(App *a, int enabled);
void music_set_enabled(App *a, int on);       // Settings › Nemo › Music (unload / reload)
void music_rescan_start(App *a);
void music_reload(App *a);                     // synchronous rescan of ms0:/MUSIC, queue restored by id               // scan ms0:/MUSIC on a worker thread, swapped in by music_tick
void music_tick(App *a);                       // every frame: track end, timer, periodic save
void music_note(App *a, NoteEvent e);          // ♪ button events
void music_hprm(App *a, uint32_t key);         // headphone remote, edge-detected inside
void music_action(App *a, Action act);         // input while the Music tab is shown
void music_save_state(App *a);                 // before leaving Nemo / launching a game
void music_enter_listening(App *a);            // show the warning, turn the screen off after 3 s
int  music_playing(const App *a);              // playing (not paused): ♪ icon in the top bar
int  music_side_tracks(const App *a, int side, const int **tracks);   // tracks of a left-column entry
// Artist overview on the right (artists view, nothing opened): row 0 "All tracks", then the albums.
int  music_artist_rows(const App *a, int artist);
int  music_folder_all_tracks(const App *a, int folder, const int **tracks);   // with subfolders
int  music_side_count(const App *a);
SideKind music_side_kind(const App *a, int side, int *index);
const char *music_side_name(const App *a, int side);
void music_sleep_label(const App *a, char *out, size_t cap);
void music_draw_hints(const App *a);
void ui_nowplaying_draw(App *a);
void ui_music_overlays_draw(App *a);
void ui_music_draw(App *a);
void ui_music_cover(const App *a, int album, int x, int y, int size);
int  ui_music_album_of(const MusicLib *l, int track);

// Network tab (netctl.c / ui_network.c)
void network_init(App *a);
void network_tick(App *a);                     // every frame
void network_action(App *a, Action act);       // input while the Network tab is shown
int  network_owns_all(const App *a);           // keyboard / USB / FTP screens take L/R too
int  network_fullscreen(const App *a);         // USB / FTP screen: no tab bar
void network_draw_hints(const App *a);
void network_on_suspend(App *a);               // PSP goes to sleep: FTP and USB off
void network_on_wake(App *a);                  // PSP woke up: reload what FTP/USB unloaded
// FTP/USB free memory: stop the music (place saved), drop artwork caches; resume rescans and reloads.
void media_suspend(App *a);
void media_resume(App *a);
void library_rescan(App *a);                   // games + music from disk (caches), toast with the counts
void ui_network_draw(App *a);
// Power window (ui_power.c)
void ui_power_draw(const App *a);
// Debug box (Settings › System › Debug): RAM / CPU / FPS rows above the debug line, bottom left.
void ui_debugbox_draw(const App *a);
void ui_home_bar_draw(const App *a);   // HOME hold progress over the hint bar

void ui_menu_draw(const App *app);
void ui_folder_pick_draw(const App *app);
void ui_confirm_draw(const App *app);
void ui_osk_draw(const App *app);
void ui_settings_draw(const App *app);
void settings_init(App *a);                    // after network_init (needs has_wifi)
// Text pages (textpage.c)
void textpage_open(App *a, StrId title, const char *text);
int  textpage_action(App *a, Action act);     // 1 = handled
void textpage_draw(const App *a);
// Saved data (ui_saves.c): game_code NULL = every game
void saves_open(App *a, const char *game_code, const char *title);
void saves_action(App *a, Action act);
void saves_draw(const App *a);
void saves_draw_hints(const App *a);
int  saves_delete_code(const char *code);     // every save of a game; count deleted
// Battery (ui_battery.c, 1.2.2): details, capacity test with its own load, results
typedef enum { BATS_DETAILS, BATS_TEST, BATS_RUNNING, BATS_RESULTS } BatScreenKind;
void battery_open(App *a, BatScreenKind k);
void battery_action(App *a, Action act);      // a running test owns every button (HOME too)
void battery_draw(const App *a);
void battery_draw_hints(const App *a);
void battery_tick(App *a);                     // every frame: readings, one log line a minute
int  battery_running(const App *a);
void battery_startup_check(App *a);            // a log left open by a self power-off: result toast
void cc_deploy(App *a);                        // Control Center plugin file + PLUGINS.TXT line (ccdeploy.c, 1.2.4)
void battery_learn_tick(App *a);               // every frame: Nemo's drain model learns on battery
// Game card menu (gamemenu.c)
void gamemenu_open(App *a);
void gamemenu_action(App *a, Action act);
void gamemenu_draw(const App *a);
void gamemenu_draw_hints(const App *a);
// Nemo instead of XMB / ARK update protection (selfguard.c)
void selfguard_start(App *a, const char *run_path);   // after the library is loaded
void selfguard_tick(App *a);                          // every frame
void selfguard_clean_exit(void);                      // Nemo leaves on purpose: not a failed start
void selfguard_action(App *a, Action act);
void selfguard_draw(const App *a);
int selfguard_busy(void);                             // backup copy still writing
// Plugin manager (ui_plugins.c)
void plugins_open(App *a);
void plugins_action(App *a, Action act);
void plugins_draw(const App *a);
void plugins_draw_hints(const App *a);
void settings_load_language(void);             // Nemo follows the system language (RU, else EN)
void settings_action(App *a, Action act);
void settings_clock_prompt_action(App *a, Action act);
void settings_draw_overlays(const App *a);
void settings_draw_hints(const App *a);
void settings_open_ark(App *a);
void settings_exit_to_xmb(App *a);
void power_do(App *a, int item);               // PowerItem from the HOME window or Settings › Power
void network_toggle_wpa2(App *a);

#endif
