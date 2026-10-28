#ifndef NEMO_MEMSTAT_H
#define NEMO_MEMSTAT_H

#include <stddef.h>
#include <stdint.h>

// "About PSP › Memory" (1.2.6): what uses the memory card, by category, and the FAT boot sector.
// Categories in rule order (first match wins); MC_OTHER is what is left of the used space.
typedef enum { MC_SYS, MC_ISO, MC_DLC, MC_GAME, MC_SAVE, MC_MUSIC, MC_PHOTO, MC_VIDEO, MC_OTHER, MC_COUNT } MemCat;

// `rel`: path from the card root without a leading '/' ("PSP/GAME/ULES01404/WORLD/a.edat"), any case.
// `game_has_eboot` matters only for PSP/GAME/<folder>/...: a folder without EBOOT.PBP holds DLC.
MemCat memstat_category(const char *rel, int game_has_eboot);

typedef struct { long long bytes[MC_COUNT]; long files; } MemTotals;

void memstat_add(MemTotals *t, MemCat c, long long size, uint32_t cluster);   // rounded up to the cluster
void memstat_finish(MemTotals *t, long long used);                            // MC_OTHER = used - the rest, >= 0
int  memstat_order(const MemTotals *t, MemCat out[MC_COUNT]);                 // non-empty, biggest first
void memstat_pct_text(char *out, size_t cap, long long part, long long total, int lang_en);   // "33 %", "<1 %"

typedef enum { MFS_UNKNOWN, MFS_FAT12, MFS_FAT16, MFS_FAT32, MFS_EXFAT } MemFs;

const char *memstat_fs_name(MemFs fs);                                        // "FAT32", "" for unknown
int   memstat_parse_boot(const uint8_t sec[512], MemFs *fs, char label[12]);   // 0, or -1 if not a boot sector
MemFs memstat_fs_from_clusters(uint32_t clusters);                            // Microsoft's FAT type rule

#endif
