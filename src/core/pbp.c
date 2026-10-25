#include "pbp.h"
#include <stdlib.h>
#include <string.h>

// Sections larger than this are never loaded into memory (icons/backgrounds are small).
#define PBP_MAX_SECTION (4u * 1024u * 1024u)

int pbp_read_header(FILE *f, PbpHeader *h) {
    uint8_t raw[0x28];
    if (!f || fseek(f, 0, SEEK_END) != 0) return -1;
    long end = ftell(f);
    if (end < 0x28 || fseek(f, 0, SEEK_SET) != 0) return -1;
    if (fread(raw, 1, sizeof raw, f) != sizeof raw) return -1;
    if (memcmp(raw, "\0PBP", 4) != 0) return -1;
    h->file_size = (uint32_t)end;
    uint32_t prev = 0x28;
    for (int i = 0; i < 8; i++) {
        const uint8_t *p = raw + 8 + i * 4;
        h->off[i] = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
        if (h->off[i] < prev || h->off[i] > h->file_size) return -1;
        prev = h->off[i];
    }
    return 0;
}

uint8_t *pbp_read_section(FILE *f, const PbpHeader *h, int section, size_t *size) {
    if (section < 0 || section > 7) return NULL;
    uint32_t start = h->off[section];
    uint32_t stop = section == 7 ? h->file_size : h->off[section + 1];
    uint32_t len = stop - start;
    if (len == 0 || len > PBP_MAX_SECTION) return NULL;
    uint8_t *buf = malloc(len);
    if (!buf) return NULL;
    if (fseek(f, (long)start, SEEK_SET) != 0 || fread(buf, 1, len, f) != len) {
        free(buf);
        return NULL;
    }
    *size = len;
    return buf;
}
