#ifndef NEMO_PHOTO_HW_H
#define NEMO_PHOTO_HW_H

#include "photos.h"
#include <SDL2/SDL.h>
#include <stdint.h>

// Photos tab on the PSP (1.3.1): decoding, rotation into the file, thumbnails, deletion.
void photo_move_old_once(void);   // old screenshots into PICTURE/Nemo/{Games,Screenshots}, once (load thread)

#define PHOTO_VIEW_MAX 1440       // longest side kept for viewing (RGB24: 1440x1080 = 4.7 MB)

// A decoded photo for the viewer: packed RGB24, possibly scaled down from full_w x full_h.
typedef struct { uint8_t *rgb; int w, h, full_w, full_h; } PhotoImage;

int  photo_size(const char *path, int *w, int *h);   // from the header, nothing decoded; 0 ok
int  photo_load(const char *path, int max_side, PhotoImage *out);   // 0 ok, -1 failed, -2 too large for memory
void photo_image_free(PhotoImage *img);

typedef enum { PR_OK, PR_BIG, PR_NO_SPACE, PR_FAIL, PR_CANCELLED } PhotoRotateResult;
// In memory when the photo fits twice; PR_BIG = too large for that (file untouched). The file date is kept.
PhotoRotateResult photo_rotate_file(const char *path, int cw);
// Large photos: in bands on a thread. start: PR_OK (running), PR_NO_SPACE or PR_FAIL.
PhotoRotateResult photo_rotate_big_start(const char *path, int w, int h, int cw);
int  photo_rotate_big_state(int *percent);   // -1 = running, else the PhotoRotateResult
void photo_rotate_big_cancel(void);
void photo_leftover(const char *path, void *ctx);   // photos_scan_ex callback: recover / remove a leftover

// Thumbnails: made on a thread, cached on the card; textures kept for the photos near the cursor.
SDL_Texture *photo_thumb(const Photo *p);   // NULL until ready (asks for it); a failed one stays NULL
void photo_thumbs_tick(void);               // main thread, every frame: ready thumbnails become textures
void photo_thumbs_stop(void);               // leaving the tab: queue emptied, textures freed
void photo_thumbs_pause(int on);            // the viewer / a rotation: no decoding meanwhile
int  photo_thumbs_busy(void);               // a thumbnail is being made or waits: USB must wait

int  photo_delete(const Photo *p);   // the file and its thumbnail; 0 ok

#endif
