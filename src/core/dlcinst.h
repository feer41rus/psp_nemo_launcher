#ifndef NEMO_DLCINST_H
#define NEMO_DLCINST_H

// "Install DLC" (1.2.5): DLC packs dropped into PSP/NEMO/install_dlc are found for one game (by its ID, or by
// title for another version of the game) and merged into PSP/GAME/<game ID>; a pack is deleted only after
// every copied file was checked.
typedef enum { DLC_SAME, DLC_OTHER } DlcMatch;

typedef struct {
    char dir[256];       // folder with the PARAM.PBP
    char pack[256];      // its top folder in the install folder (deleted after the install)
    char disc_id[16];
    DlcMatch match;
    int encrypted;       // has an encrypted file without a licence: will not work, not installed
} DlcPack;

int dlc_find(const char *root, const char *license_dir, const char *game_id, const char *game_title, DlcPack *out,
             int max);

typedef void (*DlcProgress)(void *ctx, int percent, const char *file);
// Installs the packs that are not encrypted; DLC_OTHER packs get the game's DISC_ID. Packs installed, -1 none.
// manifest (NULL: none): every installed DLC is recorded there with its files (dlcman.h).
int dlc_install(const DlcPack *packs, int n, const char *root, const char *game_dir, const char *game_id,
                const char *manifest, const char *game_title, DlcProgress cb, void *ctx);
int dlc_installed(const char *game_dir);   // 1: a DLC PARAM.PBP (CATEGORY MG) is there

#endif
