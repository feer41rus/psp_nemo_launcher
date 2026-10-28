#ifndef NEMO_SETMENU_H
#define NEMO_SETMENU_H

#include "i18n.h"
#include "input.h"

// Settings tree (author's order), navigation stack and scrolling. Items of later versions are
// SK_SOON with the version they arrive in.
typedef enum { SK_SUBMENU, SK_CHOICE, SK_ACTION, SK_INFO, SK_SOON } SetKind;
typedef enum { SID_ROOT, SID_TIME, SID_SYSTEM, SID_PLUGINS, SID_ABOUT_NEMO, SID_ABOUT_PSP, SID_POWER, SID_EXIT, SID_DATETIME, SID_DATE_FORMAT, SID_TIME_FORMAT, SID_TIMEZONE, SID_NICKNAME, SID_LANGUAGE, SID_WPA2, SID_POWERSAVE, SID_ARK, SID_NEMO, SID_OVERCLOCK, SID_CONFIRM, SID_RESET, SID_BRIGHTNESS, SID_BACKLIGHT, SID_AUTOSLEEP, SID_USBCHARGE, SID_HIGHMEM, SID_MSCACHE, SID_INFERNO, SID_SKIPLOGO, SID_NOLED, SID_NOUMD, SID_NOANALOG, SID_UMDREGION, SID_SHOW_XMB_EXIT, SID_NCL_LOGO, SID_SITE, SID_SHOW_MUSIC, SID_SHOW_FILES, SID_SHOW_VIDEO, SID_REPLACE_XMB, SID_OC_GAMES, SID_OC_XMB, SID_OC_NEMO, SID_NEMO_VERSION, SID_CREATORS, SID_TESTERS, SID_THANKS, SID_GITHUB, SID_SITE_INFO, SID_SAVES, SID_MODEL, SID_BATTERY, SID_SLEEP, SID_RESTART, SID_SHUTDOWN, SID_TO_ARK, SID_TO_XMB, SID_DEBUG, SID_DBG_RAM, SID_DBG_CPU, SID_DBG_FPS, SID_LOG_KEEP, SID_BAT_CHARGE, SID_BAT_STATE, SID_BAT_DETAILS, SID_BAT_TEST, SID_SPLASH_HELLO, SID_SPLASH_NCL, SID_UI_SOUNDS, SID_CONTROL_CENTER, SID_MEMORY, SID_MEM_FREE, SID_MEM_USAGE, SID_MEM_DETAILS, SID_MEM_FREE_INT, SID_COUNT } SetId;

typedef struct {
    SetId id;
    SetKind kind;
    StrId label;
    const char *soon;   // version for SK_SOON
    int wifi_only;
    int dropdown;       // SK_CHOICE: ✕ opens a list of values (SC_PICK) instead of stepping
    int site_only;      // shown only when Settings › Nemo › site is on
    int go_only;        // shown only on a PSP Go (1.2.6: internal storage)
} SetEntry;

#define SETMENU_MAX_ITEMS 16
#define SETMENU_DEPTH 4

typedef struct { SetId node; int sel, top; } SetLevel;
typedef struct {
    SetLevel stack[SETMENU_DEPTH];
    int depth;
    int has_wifi;
    int show_site;      // nemoleague.ru row in "About Nemo"
    int is_go;          // PSP Go: internal storage rows
} SetMenu;

typedef enum { SC_NONE, SC_OPEN, SC_ACTIVATE, SC_CHANGE, SC_SOON, SC_PICK } SetCmdType;
typedef struct { SetCmdType type; SetId id; int delta; } SetCmd;

void setmenu_init(SetMenu *m, int has_wifi);
const SetEntry *setmenu_entry(SetId id);
int  setmenu_items(const SetMenu *m, SetId node, const SetEntry **out, int max);   // visible children
SetId setmenu_node(const SetMenu *m);       // node shown now
SetId setmenu_current(const SetMenu *m);    // entry under the cursor
// ↑↓ move; ✕ opens / activates / changes (+1) / reports SOON; → opens or changes +1;
// ← changes −1 on a choice, else goes back; ○ goes back (root: nothing).
SetCmd setmenu_handle(SetMenu *m, Action a, int visible_rows);
// Scrollbar thumb for a list of count rows with top first visible; thumb_h 0 = no bar needed.
void setmenu_scrollbar(int count, int top, int visible, int track_h, int *thumb_y, int *thumb_h);

#endif
