#ifndef NEMO_GAMEFILES_H
#define NEMO_GAMEFILES_H

#include "game.h"

// What "Delete game" may remove (1.1.10): ISO/CSO files inside <dev>/ISO/, EBOOT games' own
// folders inside <dev>/PSP/GAME/ (every disc of a PS1 group). Anything else is refused.
typedef struct { char path[256]; int is_dir; } GameTarget;
int game_delete_targets(const Game *g, GameTarget *out, int max);   // count, -1 refused

// SAVEDATA folder -> game code ("ULES01020001" -> "ULES01020"); -1 for ARK_01234 and other folders.
int save_game_code(const char *dir_name, char out[10]);

// Nemo instead of XMB (1.1.11): boot counter (+1 at start, 0 after a healthy run). Returns the
// count to store; *disable = 1 when two starts in a row died while the replacement was on.
int bootguard_on_start(int count, int replace_on, int *disable);

#endif
