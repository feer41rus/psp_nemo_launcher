#ifndef NEMO_GAMESIZE_H
#define NEMO_GAMESIZE_H

#include <stddef.h>
#include "game.h"

// Game card (1.2.5): the file format and how much space the game takes.
const char *game_format_label(const Game *g);   // ISO, CSO, PSN, EBOOT, PS1 EBOOT, UMD
long long   game_size(const Game *g);           // bytes; ISO/CSO the file, EBOOT games their folder; UMD -1
void        game_size_text(char *out, size_t cap, long long bytes, int lang_en);   // "865 МБ", "1,2 ГБ"

#endif
