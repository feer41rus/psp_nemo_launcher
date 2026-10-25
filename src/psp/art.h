#ifndef NEMO_ART_H
#define NEMO_ART_H

#include <SDL2/SDL.h>
#include "game.h"
#include "music.h"

// Lazily loaded game artwork (ICON0 / PIC1) with a small LRU texture cache.
// Getters return NULL until the image is loaded; art_tick loads one pending image per call.
void art_reset(void);
SDL_Texture *art_icon(int game);
SDL_Texture *art_background(int game);
SDL_Texture *art_cover(int album);           // album cover (embedded or folder image), NULL until loaded / if none
void art_tick(const GameList *games, const MusicLib *music);

#endif
