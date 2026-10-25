#include "blockdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CSO_PLAIN_FLAG 0x80000000u
#define CSO_HEADER_SIZE 24
#define INDEX_WINDOW 512   // index entries read at once (on demand, not the whole index)

struct BlockDev {
    FILE *f;
    int cso;
    uint32_t sectors;
    uint8_t align;
    uint32_t window_first;                // CSO: first index entry held in window
    uint32_t window_count;
    uint32_t window[INDEX_WINDOW + 1];
    uint8_t zbuf[SECTOR_SIZE * 2];
};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int open_cso(BlockDev *d) {
    uint8_t h[24];
    if (fseek(d->f, 0, SEEK_SET) != 0 || fread(h, 1, sizeof h, d->f) != sizeof h) return -1;
    uint64_t total = rd32(h + 8) | (uint64_t)rd32(h + 12) << 32;
    uint32_t block = rd32(h + 16);
    if (block != SECTOR_SIZE || total == 0 || total > 4ull * 1024 * 1024 * 1024 || h[21] > 8)
        return -1;
    d->sectors = (uint32_t)((total + SECTOR_SIZE - 1) / SECTOR_SIZE);
    d->align = h[21];
    d->window_count = 0;
    return 0;
}

// Index entries lba and lba+1, reading a window of the index from disk when needed.
static int cso_index(BlockDev *d, uint32_t lba, uint32_t *a, uint32_t *b) {
    if (d->window_count == 0 || lba < d->window_first || lba + 1 >= d->window_first + d->window_count) {
        uint32_t count = d->sectors + 1 - lba;
        if (count > INDEX_WINDOW + 1) count = INDEX_WINDOW + 1;
        uint8_t raw[(INDEX_WINDOW + 1) * 4];
        if (fseek(d->f, (long)(CSO_HEADER_SIZE + (uint64_t)lba * 4), SEEK_SET) != 0) return -1;
        if (fread(raw, 4, count, d->f) != count) return -1;
        for (uint32_t i = 0; i < count; i++) d->window[i] = rd32(raw + i * 4);
        d->window_first = lba;
        d->window_count = count;
    }
    *a = d->window[lba - d->window_first];
    *b = d->window[lba + 1 - d->window_first];
    return 0;
}

BlockDev *blockdev_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    BlockDev *d = calloc(1, sizeof *d);
    if (!d) { fclose(f); return NULL; }
    d->f = f;
    char magic[4] = { 0 };
    if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "CISO", 4) == 0) {
        d->cso = 1;
        if (open_cso(d) != 0) { blockdev_close(d); return NULL; }
    } else {
        if (fseek(f, 0, SEEK_END) != 0) { blockdev_close(d); return NULL; }
        long end = ftell(f);
        if (end < 17 * SECTOR_SIZE) { blockdev_close(d); return NULL; }
        d->sectors = (uint32_t)(end / SECTOR_SIZE);
    }
    return d;
}

static int read_cso_block(BlockDev *d, uint32_t lba, uint8_t *out) {
    uint32_t a, b;
    if (cso_index(d, lba, &a, &b) != 0) return -1;
    uint64_t pos = (uint64_t)(a & ~CSO_PLAIN_FLAG) << d->align;
    uint64_t next = (uint64_t)(b & ~CSO_PLAIN_FLAG) << d->align;
    if (next < pos || fseek(d->f, (long)pos, SEEK_SET) != 0) return -1;
    if (a & CSO_PLAIN_FLAG) return fread(out, 1, SECTOR_SIZE, d->f) == SECTOR_SIZE ? 0 : -1;
    uint64_t len = next - pos;
    if (len == 0 || len > sizeof d->zbuf) return -1;
    if (fread(d->zbuf, 1, (size_t)len, d->f) != len) return -1;
    z_stream z;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) return -1;
    z.next_in = d->zbuf;
    z.avail_in = (uInt)len;
    z.next_out = out;
    z.avail_out = SECTOR_SIZE;
    int rc = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return (rc == Z_STREAM_END || rc == Z_OK || rc == Z_BUF_ERROR) && z.avail_out == 0 ? 0 : -1;
}

int blockdev_read(BlockDev *d, uint32_t lba, uint32_t count, uint8_t *out) {
    if (!d || lba >= d->sectors || count > d->sectors - lba) return -1;
    if (!d->cso) {
        if (fseek(d->f, (long)lba * SECTOR_SIZE, SEEK_SET) != 0) return -1;
        return fread(out, SECTOR_SIZE, count, d->f) == count ? 0 : -1;
    }
    for (uint32_t i = 0; i < count; i++)
        if (read_cso_block(d, lba + i, out + (size_t)i * SECTOR_SIZE) != 0) return -1;
    return 0;
}

uint32_t blockdev_sectors(BlockDev *d) { return d ? d->sectors : 0; }

void blockdev_close(BlockDev *d) {
    if (!d) return;
    if (d->f) fclose(d->f);
    free(d);
}
