#ifndef NEMO_LIBBG_HW_H
#define NEMO_LIBBG_HW_H

#include <SDL2/SDL.h>

// Library background (1.3.8): one of Nemo's pictures (assets/backgrounds), or the one the Gallery made into
// PSP/NEMO/library_bg.png (LIBBG_OWN).
int  libbg_make(const char *photo_path);   // decode, cut to the screen, matte coat, save; 0 ok
int  libbg_exists(void);                   // a background made from the Gallery is on the card
SDL_Texture *libbg_texture(int choice);    // a LibBg; loaded once, kept until the choice changes
void libbg_forget(void);                   // drop the texture: the next draw loads the current choice again

#endif
