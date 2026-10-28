#ifndef NEMO_GAME_H
#define NEMO_GAME_H

typedef enum { GK_PSP, GK_PS1, GK_HOMEBREW, GK_UTILITY } GameKind;
// GF_UMD (1.2.5): the disc in the drive, id "umd:<DISC_ID>", added and removed while Nemo runs.
typedef enum { GF_ISO, GF_PBP_PSN, GF_PBP_PS1, GF_PBP_HOMEBREW, GF_UMD } GameFormat;  // decides launch path

#define GAME_MAX_DISCS 8

typedef struct {
    char path[256];
    int number;
} GameDisc;

typedef struct {
    char id[160];        // stable key: "disc:<DISC_ID>" for grouped PS1, else lowercase path
    char title[128];     // UTF-8, disc tag stripped when grouped
    GameKind kind;
    GameFormat format;
    char path[256];      // launch path (disc 1 for groups)
    char disc_id[16];
    GameDisc discs[GAME_MAX_DISCS];
    int disc_count;      // 0 = single disc
} Game;

typedef struct {
    Game *items;
    int count, cap;
} GameList;

void gamelist_init(GameList *l);
Game *gamelist_push(GameList *l);            // zeroed slot, NULL on OOM
void gamelist_free(GameList *l);
int gamelist_find(const GameList *l, const char *id);
void gamelist_sort_by_title(GameList *l);

GameKind game_kind_for(GameFormat fmt, const char *folder_name);
int disc_parse_number(const char *title, const char *folder);
void title_strip_disc_tag(char *title);
void games_group_discs(GameList *l);

#endif
