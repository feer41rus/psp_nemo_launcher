#ifndef NEMO_PHOTOFILE_H
#define NEMO_PHOTOFILE_H

// Photo files (1.3.1): crash-safe replacement after a rotation, and the one-time move of old screenshots.

// Writes the new contents into tmp_path; 0 on success.
typedef int (*PhotoWriteFn)(void *ctx, const char *tmp_path);

// <path>.tmp is written and must not be empty; then <path> -> <path>.bak, <path>.tmp -> <path>, .bak
// removed (rename cannot overwrite on a memory stick). On any failure the original stays and the .tmp
// is removed. 0 / -1.
int photofile_replace(const char *path, PhotoWriteFn write, void *ctx);
// The same with other suffixes (1.3.2 editor: ".nemo-tmp"/".nemo-bak", so the user's own .bak files stay).
int photofile_replace_sfx(const char *path, const char *tmp_sfx, const char *bak_sfx, PhotoWriteFn write, void *ctx);

// A leftover of an interrupted replace: "<photo>.bak" goes back to <photo> when the photo is missing
// (power lost between the two renames), any other .bak/.tmp/.rot is removed. 1 = a photo restored.
int photofile_recover(const char *leftover);

// Moves the files with extension ext (any case) right inside from_dir (not its subfolders) into to_dir,
// creating it; a taken name becomes <stem>_2<ext>, _3, ... Returns how many were moved.
int photofile_count_ext(const char *dir, const char *ext);   // files (not folders) with that extension

// rename() first; when the stick refuses a move between folders: copy, compare sizes, delete the original.
int photofile_move_old(const char *from_dir, const char *to_dir, const char *ext, int (*mkdir_fn)(const char *));

#endif
