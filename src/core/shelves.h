#ifndef NEMO_SHELVES_H
#define NEMO_SHELVES_H

#include "game.h"
#include "library.h"

#define SHELF_MAX_GAMES 256

typedef enum { SHELF_RECENT, SHELF_FOLDER, SHELF_PSP, SHELF_PS1, SHELF_HOMEBREW, SHELF_UTILITY } ShelfKind;

typedef struct {
    ShelfKind kind;
    int folder_idx;                 // SHELF_FOLDER only
    int count;
    int games[SHELF_MAX_GAMES];     // GameList indices
} Shelf;

// Home: "Recent" (without the continue game, no utilities), then non-empty user folders.
// Nothing launched yet and no folder shelves: PSP, PS1, Homebrew type shelves instead.
// continue_game: most recent existing non-utility game, -1 if none.
int shelves_home(const GameList *gl, const Library *lib, Shelf *out, int max, int *continue_game);

// Library: all folders (even empty), then PSP, PS1, Homebrew, Utilities (empty type shelves omitted).
int shelves_library(const GameList *gl, const Library *lib, Shelf *out, int max);

#endif
