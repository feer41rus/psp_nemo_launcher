#include "gfx.h"
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <string.h>
#include <pspdisplay.h>
#include "utf8.h"

extern const unsigned char font_regular[], font_semibold[];
extern const unsigned int font_regular_size, font_semibold_size;

#define TEXT_CACHE 128

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_Font *fonts[FONT_COUNT];

typedef struct {
    uint32_t hash;
    FontId font;
    uint32_t color;
    SDL_Texture *tex;
    int w, h;
    uint32_t used;
    char text[96];
} CachedText;

static CachedText cache[TEXT_CACHE];
static uint32_t frame_no;

#define R(c) ((uint8_t)((c) >> 24))
#define G(c) ((uint8_t)((c) >> 16))
#define B(c) ((uint8_t)((c) >> 8))
#define A(c) ((uint8_t)(c))

// SDL's PSP renderer swizzles textures in 16-byte blocks; a texture whose row (width rounded up
// to a power of two, at least 2) is under 16 bytes gets a negative block step and corrupts
// memory. Pad such surfaces to 16 bytes per row.
//
// Format: SDL_CreateTextureFromSurface picks the renderer's first format with alpha, which on
// the PSP is ABGR1555 (1-bit alpha) — that turned gradients into hard-edged blocks and cut
// the anti-aliasing off text and icons. Anything with alpha is uploaded as ABGR8888 instead.
static SDL_Texture *texture_from_surface(SDL_Surface *s) {
    int has_alpha = s->format->Amask != 0;
    // Narrow glyphs ("i", "l", ".") came out garbled at 4 px: keep at least 8 px as well.
    int bpp = has_alpha ? 4 : s->format->BytesPerPixel;
    int min_w = 16 / (bpp ? bpp : 1);
    if (min_w < 8) min_w = 8;
    if (!has_alpha && s->w >= min_w) return SDL_CreateTextureFromSurface(renderer, s);

    Uint32 fmt = has_alpha ? SDL_PIXELFORMAT_ABGR8888 : s->format->format;
    int w = s->w < min_w ? min_w : s->w;
    SDL_Surface *conv = SDL_CreateRGBSurfaceWithFormat(0, w, s->h, SDL_BITSPERPIXEL(fmt), fmt);
    if (!conv) return NULL;
    SDL_FillRect(conv, NULL, 0);
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(s, NULL, conv, NULL);
    SDL_Texture *t = SDL_CreateTexture(renderer, fmt, SDL_TEXTUREACCESS_STATIC, conv->w, conv->h);
    if (t) {
        SDL_UpdateTexture(t, NULL, conv->pixels, conv->pitch);
        SDL_SetTextureBlendMode(t, has_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    }
    SDL_FreeSurface(conv);
    return t;
}

static TTF_Font *open_font(const unsigned char *data, unsigned size, int px) {
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    return rw ? TTF_OpenFontRW(rw, 1, px) : NULL;
}

int gfx_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    window = SDL_CreateWindow("Nemo", 0, 0, SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!window) return -1;
    // SDL defaults the PSP display to 16-bit BGR565, which bands anti-aliased text.
    // A fullscreen ABGR8888 mode makes the renderer use a 32-bit framebuffer.
    SDL_DisplayMode mode = { SDL_PIXELFORMAT_ABGR8888, SCREEN_W, SCREEN_H, 60, NULL };
    SDL_SetWindowDisplayMode(window, &mode);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return -1;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (TTF_Init() != 0) return -1;
    IMG_Init(IMG_INIT_PNG);
    fonts[FONT_SMALL] = open_font(font_regular, font_regular_size, 11);
    fonts[FONT_BODY] = open_font(font_regular, font_regular_size, 13);
    fonts[FONT_TITLE] = open_font(font_semibold, font_semibold_size, 20);
    fonts[FONT_BOLD] = open_font(font_semibold, font_semibold_size, 13);
    fonts[FONT_TINY] = open_font(font_regular, font_regular_size, 9);
    for (int i = 0; i < FONT_COUNT; i++) {
        if (!fonts[i]) return -1;
        TTF_SetFontHinting(fonts[i], TTF_HINTING_LIGHT);
    }
    return 0;
}

void gfx_quit(void) {
    for (int i = 0; i < TEXT_CACHE; i++) if (cache[i].tex) SDL_DestroyTexture(cache[i].tex);
    for (int i = 0; i < FONT_COUNT; i++) if (fonts[i]) TTF_CloseFont(fonts[i]);
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

SDL_Renderer *gfx_renderer(void) { return renderer; }

void gfx_frame_begin(void) {
    frame_no++;
    SDL_SetRenderDrawColor(renderer, R(COL_BG), G(COL_BG), B(COL_BG), 255);
    SDL_RenderClear(renderer);
}

void gfx_frame_end(void) { SDL_RenderPresent(renderer); }

// SDL's PSP renderer does not implement SDL_RenderReadPixels, so read the framebuffer that is
// on screen right now. Call right after gfx_frame_end().
int gfx_screenshot(const char *path) {
    void *top = NULL;
    int width = 0, format = 0;
    if (sceDisplayGetFrameBuf(&top, &width, &format, PSP_DISPLAY_SETBUF_NEXTFRAME) < 0 || !top) return -1;
    if (format != PSP_DISPLAY_PIXEL_FORMAT_8888) return -2;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return -1;
    const uint32_t *vram = (const uint32_t *)((uintptr_t)top | 0x40000000u);   // uncached view
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
        for (int x = 0; x < SCREEN_W; x++) row[x] = vram[y * width + x] | 0xff000000u;
    }
    int rc = IMG_SavePNG(s, path);
    SDL_FreeSurface(s);
    return rc;
}

static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

// Returns a cached texture for (font, color, text); renders on miss (LRU eviction).
static CachedText *text_entry(FontId f, uint32_t rgba, const char *utf8) {
    uint32_t h = hash_str(utf8);
    int victim = 0;
    for (int i = 0; i < TEXT_CACHE; i++) {
        CachedText *c = &cache[i];
        if (c->tex && c->hash == h && c->font == f && c->color == rgba && strcmp(c->text, utf8) == 0) {
            c->used = frame_no;
            return c;
        }
        if (!cache[i].tex || cache[i].used < cache[victim].used) victim = i;
    }
    SDL_Color col = { R(rgba), G(rgba), B(rgba), A(rgba) };
    SDL_Surface *s = TTF_RenderUTF8_Blended(fonts[f], utf8, col);
    if (!s) return NULL;
    CachedText *c = &cache[victim];
    if (c->tex) SDL_DestroyTexture(c->tex);
    c->tex = texture_from_surface(s);
    c->w = s->w;
    c->h = s->h;
    SDL_FreeSurface(s);
    c->hash = h;
    c->font = f;
    c->color = rgba;
    c->used = frame_no;
    utf8_copy(c->text, sizeof c->text, utf8);
    if (strlen(utf8) >= sizeof c->text) c->hash = 0;   // too long to match safely next time
    return c->tex ? c : NULL;
}

void gfx_text(FontId f, int x, int y, uint32_t rgba, const char *utf8) {
    if (!utf8 || !*utf8) return;
    CachedText *c = text_entry(f, rgba, utf8);
    if (!c) return;
    // Narrow texts are padded to 8 px (texture_from_surface): copy only the text, not the padding,
    // otherwise "r" was squeezed into a "'" and "j" into a ".".
    SDL_Rect src = { 0, 0, c->w, c->h }, dst = { x, y, c->w, c->h };
    SDL_RenderCopy(renderer, c->tex, &src, &dst);
}

// Measuring text shapes it with HarfBuzz, which is expensive: cache widths and fitted strings.
#define WIDTH_CACHE 256
#define FIT_CACHE 96
static struct { uint32_t hash; FontId font; int w; char text[96]; } widths[WIDTH_CACHE];
static struct { uint32_t hash; FontId font; int max_w; uint32_t used; char text[96]; char fitted[128]; } fits[FIT_CACHE];

int gfx_text_width(FontId f, const char *utf8) {
    if (!utf8 || !*utf8) return 0;
    uint32_t h = hash_str(utf8) ^ (uint32_t)f;
    int long_text = strlen(utf8) >= sizeof widths[0].text;
    unsigned slot = h % WIDTH_CACHE;
    if (!long_text && widths[slot].hash == h && widths[slot].font == f && strcmp(widths[slot].text, utf8) == 0)
        return widths[slot].w;
    int w = 0, hh = 0;
    TTF_SizeUTF8(fonts[f], utf8, &w, &hh);
    if (!long_text) {
        widths[slot].hash = h;
        widths[slot].font = f;
        widths[slot].w = w;
        strcpy(widths[slot].text, utf8);
    }
    return w;
}

int gfx_text_height(FontId f) { return TTF_FontHeight(fonts[f]); }

void gfx_text_fit(FontId f, int x, int y, int max_w, uint32_t rgba, const char *utf8) {
    if (!utf8 || !*utf8) return;
    int cacheable = strlen(utf8) < sizeof fits[0].text;
    uint32_t h = hash_str(utf8);
    int victim = 0;
    if (cacheable) {
        for (int i = 0; i < FIT_CACHE; i++) {
            if (fits[i].hash == h && fits[i].font == f && fits[i].max_w == max_w && strcmp(fits[i].text, utf8) == 0) {
                fits[i].used = frame_no;
                gfx_text(f, x, y, rgba, fits[i].fitted);
                return;
            }
            if (fits[i].used < fits[victim].used) victim = i;
        }
    }
    char buf[128];
    size_t len = utf8_copy(buf, sizeof buf - 4, utf8);
    if (gfx_text_width(f, utf8) > max_w) {
        while (len > 0) {
            len = utf8_prev(buf, len);
            strcpy(buf + len, "…");
            if (gfx_text_width(f, buf) <= max_w) break;
        }
    }
    if (cacheable) {
        fits[victim].hash = h;
        fits[victim].font = f;
        fits[victim].max_w = max_w;
        fits[victim].used = frame_no;
        strcpy(fits[victim].text, utf8);
        strcpy(fits[victim].fitted, buf);
    }
    gfx_text(f, x, y, rgba, buf);
}

void gfx_rect(int x, int y, int w, int h, uint32_t rgba) {
    SDL_SetRenderDrawColor(renderer, R(rgba), G(rgba), B(rgba), A(rgba));
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(renderer, &r);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgba) {
    roundedBoxRGBA(renderer, x, y, x + w - 1, y + h - 1, r, R(rgba), G(rgba), B(rgba), A(rgba));
}

void gfx_round_rect_outline(int x, int y, int w, int h, int r, uint32_t rgba) {
    roundedRectangleRGBA(renderer, x, y, x + w - 1, y + h - 1, r, R(rgba), G(rgba), B(rgba), A(rgba));
}

static uint8_t lerp8(uint8_t a, uint8_t b, int t, int n) { return (uint8_t)(a + (b - a) * t / n); }

// Gradients are cached as 1-pixel strips and stretched in a single draw call.
#define GRADIENT_CACHE 24
static struct { uint32_t a, b; int len, vertical; SDL_Texture *tex; uint32_t used; } gradients[GRADIENT_CACHE];

static SDL_Texture *gradient_strip(uint32_t a, uint32_t b, int len, int vertical) {
    int victim = 0;
    for (int i = 0; i < GRADIENT_CACHE; i++) {
        if (gradients[i].tex && gradients[i].a == a && gradients[i].b == b &&
            gradients[i].len == len && gradients[i].vertical == vertical) {
            gradients[i].used = frame_no;
            return gradients[i].tex;
        }
        if (!gradients[i].tex || gradients[i].used < gradients[victim].used) victim = i;
    }
    // Vertical strips are 8 px wide: see texture_from_surface about narrow textures.
    const int thick = 8;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, vertical ? thick : len, vertical ? len : thick, 32,
                                                    SDL_PIXELFORMAT_RGBA8888);
    if (!s) return NULL;
    for (int i = 0; i < len; i++) {
        int n = len > 1 ? len - 1 : 1;
        uint32_t c = (uint32_t)lerp8(R(a), R(b), i, n) << 24 | (uint32_t)lerp8(G(a), G(b), i, n) << 16 |
                     (uint32_t)lerp8(B(a), B(b), i, n) << 8 | lerp8(A(a), A(b), i, n);
        for (int k = 0; k < thick; k++) {
            int x = vertical ? k : i, y = vertical ? i : k;
            ((uint32_t *)((uint8_t *)s->pixels + y * s->pitch))[x] = c;
        }
    }
    if (gradients[victim].tex) SDL_DestroyTexture(gradients[victim].tex);
    gradients[victim].tex = texture_from_surface(s);
    SDL_FreeSurface(s);
    if (gradients[victim].tex) SDL_SetTextureBlendMode(gradients[victim].tex, SDL_BLENDMODE_BLEND);
    gradients[victim].a = a;
    gradients[victim].b = b;
    gradients[victim].len = len;
    gradients[victim].vertical = vertical;
    gradients[victim].used = frame_no;
    return gradients[victim].tex;
}

void gfx_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;
    SDL_Texture *t = gradient_strip(top, bottom, h, 1);
    SDL_Rect dst = { x, y, w, h };
    if (t) SDL_RenderCopy(renderer, t, NULL, &dst);
}

void gfx_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (w <= 0 || h <= 0) return;
    SDL_Texture *t = gradient_strip(left, right, w, 0);
    SDL_Rect dst = { x, y, w, h };
    if (t) SDL_RenderCopy(renderer, t, NULL, &dst);
}

SDL_Texture *gfx_texture_from_png(const void *data, size_t size) {
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (!rw) return NULL;
    SDL_Surface *s = IMG_Load_RW(rw, 1);
    if (!s) return NULL;
    SDL_Texture *t = texture_from_surface(s);
    SDL_FreeSurface(s);
    return t;
}

SDL_Texture *gfx_texture_from_surface(SDL_Surface *s) { return s ? texture_from_surface(s) : NULL; }

void gfx_texture(SDL_Texture *t, int x, int y, int w, int h, uint8_t alpha) {
    if (!t) return;
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, t, NULL, &dst);
}

void gfx_texture_cover(SDL_Texture *t, int x, int y, int w, int h, uint8_t alpha) {
    int tw, th;
    if (!t || SDL_QueryTexture(t, NULL, NULL, &tw, &th) != 0 || w <= 0 || h <= 0) return;
    SDL_Rect src = { 0, 0, tw, th };
    if ((long)tw * h > (long)th * w) {        // texture is wider: crop left/right
        src.w = (int)((long)th * w / h);
        src.x = (tw - src.w) / 2;
    } else {                                    // texture is taller: crop top/bottom
        src.h = (int)((long)tw * h / w);
        src.y = (th - src.h) / 2;
    }
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, t, &src, &dst);
}

static void key_cap(int cx, int cy, int w, int h, const char *label) {
    gfx_round_rect(cx - w / 2, cy - h / 2, w, h, 3, 0x2a3442ff);
    int tw = gfx_text_width(FONT_SMALL, label);
    gfx_text(FONT_SMALL, cx - tw / 2, cy - gfx_text_height(FONT_SMALL) / 2, COL_TEXT, label);
}

void gfx_button_glyph(Glyph g, int cx, int cy, int size) {
    int r = size / 2, s = size / 4 + 1;
    switch (g) {
    case GLYPH_L: key_cap(cx, cy, size + 4, size, "L"); return;
    case GLYPH_R: key_cap(cx, cy, size + 4, size, "R"); return;
    case GLYPH_START: key_cap(cx, cy, size * 3, size, "START"); return;
    case GLYPH_SELECT: key_cap(cx, cy, size * 3 + 4, size, "SELECT"); return;
    default: break;
    }
    filledCircleRGBA(renderer, cx, cy, r, 0x2a, 0x34, 0x42, 255);
    switch (g) {
    case GLYPH_CROSS:
        thickLineRGBA(renderer, cx - s, cy - s, cx + s, cy + s, 2, 0x7f, 0xb2, 0xff, 255);
        thickLineRGBA(renderer, cx - s, cy + s, cx + s, cy - s, 2, 0x7f, 0xb2, 0xff, 255);
        break;
    case GLYPH_CIRCLE:
        aacircleRGBA(renderer, cx, cy, s, 0xff, 0x6f, 0x7d, 255);
        aacircleRGBA(renderer, cx, cy, s - 1, 0xff, 0x6f, 0x7d, 255);
        break;
    case GLYPH_TRIANGLE:
        aatrigonRGBA(renderer, cx, cy - s, cx - s, cy + s - 1, cx + s, cy + s - 1, 0x5f, 0xe0, 0xb0, 255);
        break;
    case GLYPH_SQUARE:
        rectangleRGBA(renderer, cx - s + 1, cy - s + 1, cx + s - 1, cy + s - 1, 0xff, 0x9b, 0xf0, 255);
        break;
    case GLYPH_DPAD:
        gfx_rect(cx - 1, cy - s, 3, s * 2 + 1, COL_TEXT);
        gfx_rect(cx - s, cy - 1, s * 2 + 1, 3, COL_TEXT);
        break;
    case GLYPH_NOTE:
        filledCircleRGBA(renderer, cx - 2, cy + 2, 2, 0xe9, 0xee, 0xf5, 255);
        gfx_rect(cx, cy - s, 1, s + 2, COL_TEXT);
        gfx_rect(cx, cy - s, 3, 1, COL_TEXT);
        break;
    default: break;
    }
}

int gfx_status_icon(StatusIcon icon, int x, int y, int value, int charging) {
    switch (icon) {
    case ICON_BATTERY: {
        uint32_t col = value <= 15 && !charging ? 0xff6f7dff : COL_TEXT;
        rectangleRGBA(renderer, x, y, x + 15, y + 7, 0x9f, 0xb0, 0xc4, 255);
        gfx_rect(x + 16, y + 2, 2, 4, 0x9fb0c4ff);
        int fill = value < 0 ? 0 : value > 100 ? 13 : value * 13 / 100;
        gfx_rect(x + 1, y + 1, fill + (fill > 0), 6, charging ? 0x5fe0b0ff : col);
        return 19;
    }
    case ICON_WIFI: {
        // value: 0 switch off (dim), 1 on, 3 connected (accent).
        uint8_t a = value ? 255 : 90;
        uint8_t r = value == 3 ? 0x6f : 0xe9, gg = value == 3 ? 0xb0 : 0xee, b = value == 3 ? 0xff : 0xf5;
        filledCircleRGBA(renderer, x + 6, y + 8, 1, r, gg, b, a);
        arcRGBA(renderer, x + 6, y + 8, 4, 225, 315, r, gg, b, a);
        arcRGBA(renderer, x + 6, y + 8, 7, 225, 315, r, gg, b, a);
        return 13;
    }
    case ICON_NOTE:
        gfx_button_glyph(GLYPH_NOTE, x + 5, y + 4, 10);
        return 10;
    }
    return 0;
}
