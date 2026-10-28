#ifndef NEMO_DLCMAN_H
#define NEMO_DLCMAN_H

#include <stddef.h>

// Installed DLC (1.2.5): Nemo writes which files every DLC brought (PSP/NEMO/dlc/<game ID>.txt:
// "pack\t<name>" lines, each followed by "file\t<path inside PSP/GAME/<ID>>" lines), so a chosen DLC can be
// deleted later without touching the others or the game.
void dlc_pack_name(const char *folder, const char *game_title, char *out, size_t cap);

typedef struct { char name[128]; int files; long long bytes; } DlcEntry;

int dlcman_load(const char *manifest, const char *game_dir, DlcEntry *out, int max);   // DLC listed
// Adds a DLC (a DLC of the same name is replaced). files: paths separated by '\n'. 0 ok.
int dlcman_add(const char *manifest, const char *name, const char *files);
// Deletes the DLC marked in selected[0..n): their files that no other DLC lists, never EBOOT.PBP / PBOOT.PBP;
// the last one gone, the manifest goes too. Returns the DLC deleted.
int dlcman_delete(const char *manifest, const char *game_dir, const int *selected, int n);

#endif
