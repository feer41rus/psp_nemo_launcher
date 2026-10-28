#ifndef NEMO_GAME_PSP_H
#define NEMO_GAME_PSP_H

// Control Center platform for PSP games and homebrew (1.2.4). A PS1 platform can sit next to it later and
// reuse ccmenu / ccdraw / ccui.
int  ccgame_install(void);      // hooks the frame output and the pad, turns the Sony HOME window off; 0 ok
int  ccgame_buttons(void);      // real buttons, HOME included
void ccgame_block_input(int on);
int  ccgame_param(int id);      // impose parameter, -1 unknown
void ccgame_set_param(int id, int v);
void ccgame_vblank(void);
void ccgame_exit(void);         // back to Nemo (ARK launcher)
int  ccgame_lang_en(void);
int  ccgame_cross_ok(void);
int  ccgame_battery(void);      // %, -1 unknown
void ccgame_clock(int *h, int *m);

#endif
