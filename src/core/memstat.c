#include "memstat.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// 1 if `rel` is `dir` itself or lies inside it (case-insensitive, whole path components).
static int under(const char *rel, const char *dir) {
    size_t n = strlen(dir);
    return strncasecmp(rel, dir, n) == 0 && (rel[n] == '/' || rel[n] == '\0');
}

MemCat memstat_category(const char *rel, int game_has_eboot) {
    if (under(rel, "PSP/SAVEDATA/ARK_01234") || under(rel, "PSP/NEMO") || under(rel, "SEPLUGINS")) return MC_SYS;
    if (under(rel, "ISO")) return MC_ISO;
    if (under(rel, "PSP/GAME")) {
        const char *rest = rel + strlen("PSP/GAME");
        if (*rest == '/') rest++;
        if (!strchr(rest, '/')) return MC_GAME;   // a file right in PSP/GAME
        return game_has_eboot ? MC_GAME : MC_DLC;
    }
    if (under(rel, "PSP/SAVEDATA")) return MC_SAVE;
    if (under(rel, "MUSIC")) return MC_MUSIC;
    if (under(rel, "PICTURE")) return MC_PHOTO;
    if (under(rel, "VIDEO")) return MC_VIDEO;
    return MC_OTHER;
}

void memstat_add(MemTotals *t, MemCat c, long long size, uint32_t cluster) {
    if (size > 0 && cluster) size = (size + cluster - 1) / cluster * cluster;
    if (size > 0) t->bytes[c] += size;
    t->files++;
}

void memstat_finish(MemTotals *t, long long used) {
    long long sum = 0;
    for (int c = 0; c < MC_COUNT; c++)
        if (c != MC_OTHER) sum += t->bytes[c];
    t->bytes[MC_OTHER] = used > sum ? used - sum : 0;
}

int memstat_order(const MemTotals *t, MemCat out[MC_COUNT]) {
    int n = 0;
    for (int c = 0; c < MC_COUNT; c++)
        if (t->bytes[c] > 0) out[n++] = (MemCat)c;
    for (int i = 1; i < n; i++) {   // insertion sort, stable: ties keep the MemCat order
        MemCat k = out[i];
        int j = i - 1;
        while (j >= 0 && t->bytes[out[j]] < t->bytes[k]) { out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return n;
}

void memstat_pct_text(char *out, size_t cap, long long part, long long total, int lang_en) {
    const char *sp = lang_en ? "" : " ";
    if (part <= 0 || total <= 0) { snprintf(out, cap, "0%s%%", sp); return; }
    long long pct = part * 100 / total;
    if (pct > 100) pct = 100;
    if (pct < 1) snprintf(out, cap, "<1%s%%", sp);
    else snprintf(out, cap, "%lld%s%%", pct, sp);
}

const char *memstat_fs_name(MemFs fs) {
    switch (fs) {
    case MFS_FAT12: return "FAT12";
    case MFS_FAT16: return "FAT16";
    case MFS_FAT32: return "FAT32";
    case MFS_EXFAT: return "exFAT";
    case MFS_UNKNOWN: break;
    }
    return "";
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

// The 11-byte volume label without trailing spaces; "NO NAME" (the FAT default) counts as none.
static void copy_label(const uint8_t *src, char label[12]) {
    int n = 11;
    memcpy(label, src, 11);
    while (n > 0 && (label[n - 1] == ' ' || label[n - 1] == '\0')) n--;
    label[n] = '\0';
    for (int i = 0; i < n; i++)
        if ((unsigned char)label[i] < 0x20) { label[0] = '\0'; return; }
    if (strcmp(label, "NO NAME") == 0) label[0] = '\0';
}

int memstat_parse_boot(const uint8_t sec[512], MemFs *fs, char label[12]) {
    *fs = MFS_UNKNOWN;
    label[0] = '\0';
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;
    if (memcmp(sec + 3, "EXFAT   ", 8) == 0) { *fs = MFS_EXFAT; return 0; }
    if (memcmp(sec + 0x52, "FAT32   ", 8) == 0) { *fs = MFS_FAT32; copy_label(sec + 0x47, label); return 0; }
    if (memcmp(sec + 0x36, "FAT16   ", 8) == 0) { *fs = MFS_FAT16; copy_label(sec + 0x2B, label); return 0; }
    if (memcmp(sec + 0x36, "FAT12   ", 8) == 0) { *fs = MFS_FAT12; copy_label(sec + 0x2B, label); return 0; }
    uint32_t bps = le16(sec + 0x0B), per_cluster = sec[0x0D];   // no type text: a real BPB or nothing (an MBR)
    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) || !per_cluster || (per_cluster & (per_cluster - 1)))
        return -1;
    if (le16(sec + 0x16) == 0) { *fs = MFS_FAT32; return 0; }   // only FAT32 keeps the FAT size in 32 bits
    uint32_t total = le16(sec + 0x13);
    if (!total) total = le32(sec + 0x20);
    *fs = memstat_fs_from_clusters(total / per_cluster);   // data area not subtracted: close enough
    return 0;
}

MemFs memstat_fs_from_clusters(uint32_t clusters) {
    if (clusters < 4085) return MFS_FAT12;
    if (clusters < 65525) return MFS_FAT16;
    return MFS_FAT32;
}
