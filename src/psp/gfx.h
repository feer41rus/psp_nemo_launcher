#ifndef NEMO_GFX_H
#define NEMO_GFX_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include "input.h"

#define SCREEN_W 480
#define SCREEN_H 272

// Colors are 0xRRGGBBAA.
#define COL_BG        0x0e131bff
#define COL_TEXT      0xe9eef5ff
#define COL_MUTED     0x8c9bb0ff
#define COL_ACCENT    0x6fb0ffff
#define COL_PANEL     0x1d2633ff
#define COL_HINT_BG   0x00000059
#define COL_WARN      0xffcf5aff
#define COL_BAR       0x0a0e14ff

typedef enum { FONT_SMALL, FONT_BODY, FONT_TITLE, FONT_BOLD, FONT_TINY, FONT_COUNT } FontId;

typedef enum { ICON_BATTERY, ICON_WIFI, ICON_NOTE } StatusIcon;

int gfx_init(void);
void gfx_quit(void);
SDL_Renderer *gfx_renderer(void);
void gfx_frame_begin(void);
void gfx_frame_end(void);
// Save the frame currently on screen (call right after gfx_frame_end) as PNG. 0 on success.
int gfx_screenshot(const char *path);

void gfx_text(FontId f, int x, int y, uint32_t rgba, const char *utf8);
int gfx_text_width(FontId f, const char *utf8);
int gfx_text_height(FontId f);
// Draw text clipped to max_w pixels, ending with "…" when it does not fit.
void gfx_text_fit(FontId f, int x, int y, int max_w, uint32_t rgba, const char *utf8);

void gfx_rect(int x, int y, int w, int h, uint32_t rgba);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgba);
void gfx_round_rect_outline(int x, int y, int w, int h, int r, uint32_t rgba);
void gfx_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right);

SDL_Texture *gfx_texture_from_png(const void *data, size_t size);
SDL_Texture *gfx_texture_from_surface(SDL_Surface *s);
void gfx_texture(SDL_Texture *t, int x, int y, int w, int h, uint8_t alpha);
// Fill the rectangle keeping the texture's aspect ratio (crops the overflow, centered).
void gfx_texture_cover(SDL_Texture *t, int x, int y, int w, int h, uint8_t alpha);

// Physical PSP button glyph centered at (cx, cy), size = diameter in pixels.
void gfx_button_glyph(Glyph g, int cx, int cy, int size);
// Status bar icon; value: battery percent / wifi on (0/1) / unused.
int gfx_status_icon(StatusIcon icon, int x, int y, int value, int charging);

#endif
