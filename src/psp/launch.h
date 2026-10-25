#ifndef NEMO_LAUNCH_H
#define NEMO_LAUNCH_H

#include "game.h"

// Start a game (disc is 1-based for multi-disc PS1). Returns < 0 on failure; on success never returns.
int launch_game(const Game *g, int disc);
// Leave Nemo for the stock XMB. Returns -1 (and stays) if ARK's settings could not be updated.
int launch_exit_to_xmb(void);
// Start ARK's own launcher (VBOOT_ARK.PBP). Returns -1 if it is missing.
int launch_ark_menu(void);
// Restart Nemo itself (VBOOT.PBP), e.g. after Settings › Reset. Returns -1 on failure.
int launch_self(void);

#endif
