#ifndef NEMO_NAV_H
#define NEMO_NAV_H

#include "input.h"

typedef enum { TAB_HOME, TAB_LIBRARY, TAB_MUSIC, TAB_NETWORK, TAB_SETTINGS, TAB_COUNT } Tab;
typedef enum { SCR_ROOT, SCR_CARD, SCR_GAME_MENU, SCR_FOLDER_PICK, SCR_OSK, SCR_CONFIRM } ScreenKind;

#define NAV_MAX_SHELVES 64
#define NAV_MAX_DEPTH   6

// What the UI currently shows; filled by the UI before every nav_handle call.
typedef struct {
    int shelf_count;
    int shelf_len[NAV_MAX_SHELVES];
    int has_continue;       // Home shows the "Continue" banner (shelf -1)
    int settings_count;
    int menu_count;         // items in the open action menu
    int pick_count;         // items in the open folder picker
    int card_discs;         // discs of the game in the open card (0/1 = single)
    unsigned hidden_tabs;   // bit per Tab: L/R skip it, an open hidden tab falls back to Home
} NavModel;

typedef struct {
    ScreenKind kind;
    int sel;
} NavScreen;

typedef struct {
    Tab tab;
    int shelf[TAB_COUNT];   // Home: -1 = continue banner; Settings: selected row
    int tile[TAB_COUNT];
    NavScreen stack[NAV_MAX_DEPTH];
    int depth;              // 0 = tab root
    int card_game;          // set by the UI when a card opens
    int card_disc;          // 1-based
} Nav;

typedef enum {
    CMD_NONE, CMD_OPEN_CARD, CMD_LAUNCH, CMD_OPEN_MENU, CMD_MENU_PICK, CMD_OPEN_PICK,
    CMD_FOLDER_PICK, CMD_SETTING, CMD_CONFIRM_YES
} NavCmdType;

typedef struct {
    NavCmdType type;
    int index;
} NavCmd;

void nav_init(Nav *n);
NavCmd nav_handle(Nav *n, Action a, const NavModel *m);
void nav_push(Nav *n, ScreenKind kind);
void nav_pop(Nav *n);
ScreenKind nav_top(const Nav *n);

#endif
