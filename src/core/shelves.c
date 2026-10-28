#include "shelves.h"
#include <string.h>

static void shelf_push(Shelf *s, int game) {
    if (s->count < SHELF_MAX_GAMES) s->games[s->count++] = game;
}

// Appends folder shelves; library = keep empty folders, home = hide empty folders and utilities.
static int add_folders(const GameList *gl, const Library *lib, Shelf *out, int n, int max, int library) {
    for (int f = 0; f < lib->folder_count && n < max; f++) {
        Shelf *s = &out[n];
        memset(s, 0, sizeof *s);
        s->kind = SHELF_FOLDER;
        s->folder_idx = f;
        for (int i = 0; i < lib->folders[f].count; i++) {
            int g = gamelist_find(gl, lib->folders[f].ids[i]);
            if (g >= 0 && (library || gl->items[g].kind != GK_UTILITY)) shelf_push(s, g);
        }
        if (s->count > 0 || library) n++;
    }
    return n;
}

// Appends non-empty type shelves (PSP, PS1, Homebrew, then Utilities if with_utilities).
static int add_types(const GameList *gl, Shelf *out, int n, int max, int with_utilities) {
    static const struct { ShelfKind shelf; GameKind kind; } TYPES[] = {
        { SHELF_PSP, GK_PSP }, { SHELF_PS1, GK_PS1 },
        { SHELF_HOMEBREW, GK_HOMEBREW }, { SHELF_UTILITY, GK_UTILITY },
    };
    for (unsigned t = 0; t < sizeof TYPES / sizeof *TYPES && n < max; t++) {
        if (TYPES[t].kind == GK_UTILITY && !with_utilities) continue;
        Shelf *s = &out[n];
        memset(s, 0, sizeof *s);
        s->kind = TYPES[t].shelf;
        for (int g = 0; g < gl->count; g++)
            if (gl->items[g].kind == TYPES[t].kind) shelf_push(s, g);
        if (s->count > 0) n++;
    }
    return n;
}

int shelves_home(const GameList *gl, const Library *lib, Shelf *out, int max, int *continue_game) {
    int n = 0;
    *continue_game = -1;
    Shelf recent;
    memset(&recent, 0, sizeof recent);
    recent.kind = SHELF_RECENT;
    // A disc in the drive is the main game; the former main one leads the recent row (author, 1.2.5).
    for (int g = 0; g < gl->count && *continue_game < 0; g++)
        if (gl->items[g].format == GF_UMD) *continue_game = g;
    for (int i = 0; i < lib->recent_count; i++) {
        int g = gamelist_find(gl, lib->recent[i]);
        if (g < 0 || gl->items[g].kind == GK_UTILITY || gl->items[g].format == GF_UMD) continue;
        if (*continue_game < 0) *continue_game = g;
        else shelf_push(&recent, g);
    }
    if (recent.count > 0 && n < max) out[n++] = recent;
    n = add_folders(gl, lib, out, n, max, 0);
    // Nothing launched yet and no folders: show the games by type so Home isn't empty.
    if (*continue_game < 0 && n == 0) n = add_types(gl, out, n, max, 0);
    return n;
}

int shelves_library(const GameList *gl, const Library *lib, Shelf *out, int max) {
    int n = add_folders(gl, lib, out, 0, max, 1);   // empty folders stay manageable
    return add_types(gl, out, n, max, 1);
}
