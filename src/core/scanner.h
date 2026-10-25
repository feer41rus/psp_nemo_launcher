#ifndef NEMO_SCANNER_H
#define NEMO_SCANNER_H

#include "game.h"

typedef struct {
    int parsed;   // files whose metadata was read from disk
    int reused;   // files taken from the scan cache (same path, size and mtime)
} ScanStats;

// Find games under root ("ms0:" on PSP, a directory on the host):
// <root>/ISO (recursive, .iso/.cso, except ISO/VIDEO) and <root>/PSP/GAME/*/EBOOT.PBP.
// Groups multi-disc PS1 games and sorts by title. Returns the number of games.
int scan_games(const char *root, GameList *out);

// Same, but reuses and rewrites a scan cache file (JSON). cache_path/stats may be NULL.
int scan_games_cached(const char *root, const char *cache_path, GameList *out, ScanStats *stats);

#endif
