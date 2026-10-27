#ifndef NEMO_LAUNCH_H
#define NEMO_LAUNCH_H

#include "game.h"

// Start a game (disc is 1-based for multi-disc PS1). Returns < 0 on failure; on success never returns.
int launch_game(const Game *g, int disc);
// Leave Nemo for the stock XMB. Returns -1 (and stays) if ARK's settings could not be updated.
int launch_exit_to_xmb(void);
// Start ARK's own launcher (VBOOT_ARK.PBP). Returns -1 if it is missing.
int launch_ark_menu(void);
// Nemo hands the PSP to a game / the XMB / ARK: a marker file says the next Nemo start is a return, not a
// power-on (the start-up splash is shown only at power-on, 1.2.3). away = 0 removes it.
#define LAUNCH_AWAY_FILE "ms0:/PSP/NEMO/away.txt"
void launch_mark_away(int away);
// Restart Nemo itself (VBOOT.PBP), e.g. after Settings › Reset. Returns -1 on failure.
int launch_self(void);

#endif
