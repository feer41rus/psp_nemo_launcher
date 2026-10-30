// Library background (1.3.8): a Gallery picture cut to 480x272 under a matte coat, kept as a PNG on the card.
#include "libbg_hw.h"
#include "gfx.h"
#include "libbg.h"
#include "log.h"
#include "photo_hw.h"
#include "usb.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <turbojpeg.h>

#define LIBBG_PATH NEMO_DATA_DIR "/library_bg.png"
#define LIBBG_TMP NEMO_DATA_DIR "/library_bg.tmp"
#define DECODE_SIDE 1024   // enough detail for a blurred 480x272, little memory even for camera photos

// Nemo's pictures, embedded as 480x272 JPEGs (bin2c), in LibBg order.
extern const unsigned char bg_branches[], bg_rails[], bg_three_lives[], bg_particles[], bg_mars[], bg_grid[];
extern const unsigned int bg_branches_size, bg_rails_size, bg_three_lives_size, bg_particles_size, bg_mars_size,
    bg_grid_size;

static const unsigned char *const BUILTIN[LIBBG_OWN] = { bg_branches, bg_rails, bg_three_lives, bg_particles, bg_mars,
                                                         bg_grid };
static const unsigned int *const BUILTIN_SIZE[LIBBG_OWN] = { &bg_branches_size, &bg_rails_size, &bg_three_lives_size,
                                                             &bg_particles_size, &bg_mars_size, &bg_grid_size };

static SDL_Texture *tex;
static int tex_choice = -1;   // the LibBg tex holds, -1 nothing

int libbg_make(const char *photo_path) {
    if (usb_active()) return -1;   // the memory stick belongs to the PC
    PhotoImage img;
    if (photo_load(photo_path, DECODE_SIDE, &img) != 0) return -1;
    uint8_t *out = malloc((size_t)LIBBG_W * LIBBG_H * 3), *tmp = malloc((size_t)LIBBG_W * LIBBG_H * 3);
    int rc = -1;
    if (out && tmp) {
        libbg_cover(img.rgb, img.w, img.h, out);
        photo_image_free(&img);
        libbg_matte(out, tmp);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(out, LIBBG_W, LIBBG_H, 24, LIBBG_W * 3, SDL_PIXELFORMAT_RGB24);
        // Written next to the old one first: a full card or a pulled stick leaves the old background whole.
        if (s && IMG_SavePNG(s, LIBBG_TMP) == 0) {
            remove(LIBBG_PATH);
            rc = rename(LIBBG_TMP, LIBBG_PATH) == 0 ? 0 : -1;
        }
        if (s) SDL_FreeSurface(s);
        if (rc != 0) remove(LIBBG_TMP);
    }
    photo_image_free(&img);
    free(out);
    free(tmp);
    log_msg("libbg: from %s rc=%d", photo_path, rc);
    if (rc == 0) libbg_forget();
    return rc;
}

int libbg_exists(void) {
    struct stat st;
    return stat(LIBBG_PATH, &st) == 0 && st.st_size > 0;
}

void libbg_forget(void) {
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
    tex_choice = -1;
}

static SDL_Texture *builtin(int i) {
    uint8_t *rgb = malloc((size_t)LIBBG_W * LIBBG_H * 3);
    tjhandle tj = tjInitDecompress();
    SDL_Texture *t = NULL;
    if (rgb && tj && tjDecompress2(tj, BUILTIN[i], *BUILTIN_SIZE[i], rgb, LIBBG_W, 0, LIBBG_H, TJPF_RGB, 0) == 0) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(rgb, LIBBG_W, LIBBG_H, 24, LIBBG_W * 3, SDL_PIXELFORMAT_RGB24);
        if (s) {
            t = gfx_texture_from_surface(s);
            SDL_FreeSurface(s);
        }
    }
    if (tj) tjDestroy(tj);
    free(rgb);
    return t;
}

SDL_Texture *libbg_texture(int choice) {
    choice = libbg_valid(choice);
    if (tex_choice == choice) return tex;
    libbg_forget();
    if (choice == LIBBG_OWN && !usb_active()) {
        SDL_Surface *s = IMG_Load(LIBBG_PATH);
        if (s) {
            tex = gfx_texture_from_surface(s);
            SDL_FreeSurface(s);
        }
        if (!tex) log_msg("libbg: %s unreadable, Nemo's first picture instead", LIBBG_PATH);
    }
    if (!tex) tex = builtin(choice == LIBBG_OWN ? LIBBG_BRANCHES : choice);
    tex_choice = choice;   // even on a failure: no reload attempt every frame
    return tex;
}
