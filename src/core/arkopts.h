#ifndef NEMO_ARKOPTS_H
#define NEMO_ARKOPTS_H

// ARK's SETTINGS.TXT options with variants (highmem:auto, mscache:8k, …) and CPU clocks per
// level, written the way ARK's own settings app writes them. Only global lines (runlevel scopes)
// of the edited option change; per-game lines ("ULUS10201, infernocache, off") stay.
typedef struct { const char *key; int on; } ArkLine;   // key NULL = no line (ARK default)
typedef struct {
    const char *family;     // key, or key prefix for families like "region_"
    int count;
    ArkLine choices[6];
    int plain_on;           // choice meant by "always, <family>, on" (-1 none)
} ArkOpt;

extern const ArkOpt ARK_OPT_USBCHARGE, ARK_OPT_HIGHMEM, ARK_OPT_MSCACHE, ARK_OPT_INFERNO, ARK_OPT_SKIPLOGOS,
    ARK_OPT_NOLED, ARK_OPT_NOUMD, ARK_OPT_NOANALOG, ARK_OPT_REGION;

// ARK-5's own SETTINGS.TXT (FasterARK distribution, PSP/SAVEDATA/ARK_01234/SETTINGS.TXT).
extern const char ARK_FACTORY_SETTINGS[];
// Factory text plus the user's per-game lines (scope = game IDs) that the factory file lacks.
char *arkopts_factory_reset(const char *current);

int   arkopt_get(const char *text, const ArkOpt *o);               // choice index (last "always" line wins)
char *arkopt_set(const char *text, const ArkOpt *o, int choice);   // malloc'd new text

typedef enum { CLK_GAME, CLK_VSH, CLK_LAUNCHER } ClockTarget;
extern const int ARK_CLOCKS[8];                                    // 0 (auto), 133, 222, 333, 383, 403, 423, 443
int   arkclock_get(const char *text, ClockTarget t);               // MHz, 0 = auto
char *arkclock_set(const char *text, ClockTarget t, int mhz);      // malloc'd new text
// One game's clock: "<GAMEID>, cpuclock:N, on" (1.1.10 card); 0 = none (global clock applies).
int   arkclock_game_get(const char *text, const char *game_id);
char *arkclock_game_set(const char *text, const char *game_id, int mhz);

#endif
