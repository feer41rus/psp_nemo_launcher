#ifndef NEMO_PHOTOS_H
#define NEMO_PHOTOS_H

#include <stddef.h>

// Photos tab (1.3.1): every picture under the given roots (PICTURE, DCIM), newest first.
typedef enum { PS_GAMES, PS_NEMO, PS_CAMERA, PS_UPLOADED } PhotoSource;

typedef struct {
    char path[256];
    long long size;
    long mtime;
    PhotoSource src;
} Photo;

typedef struct { Photo *items; int count, cap; } PhotoList;

#define PHOTOS_MAX_DEPTH 6

int  photo_is_image(const char *name);          // .jpg .jpeg .png .bmp, any case, not a dot file
PhotoSource photo_source(const char *path);     // by folder: Nemo/Games, Nemo, DCIM, the rest
int  photos_scan(PhotoList *l, const char *const *roots, int n_roots);   // replaces the list; count
// Also reports leftovers of an interrupted rotation (<photo>.tmp / .bak / .rot) to on_leftover.
int  photos_scan_ex(PhotoList *l, const char *const *roots, int n_roots,
                    void (*on_leftover)(const char *path, void *ctx), void *ctx);
// 1.3.2, opened from the file manager: the pictures of one folder (not below it), by name ignoring case.
int  photos_folder(PhotoList *l, const char *dir);           // replaces the list; count
int  photos_index_of(const PhotoList *l, const char *name);  // by file name, 0 if absent
int  photos_remove(PhotoList *l, int index);    // new focus (a neighbour), -1 when empty
void photos_free(PhotoList *l);
void photo_thumb_key(const Photo *p, char *out, size_t cap);   // 16 hex digits of path+size+mtime

#endif
