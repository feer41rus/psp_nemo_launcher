#include "iso9660.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ISO_MAX_FILE (8u * 1024u * 1024u)
#define ISO_MAX_DIR  (256u * 1024u)

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Compare a path component with an ISO9660 identifier ("NAME.EXT;1").
static int name_matches(const char *want, size_t want_len, const uint8_t *id, size_t id_len) {
    size_t n = id_len;
    for (size_t i = 0; i < id_len; i++) if (id[i] == ';') { n = i; break; }
    if (n > 0 && id[n - 1] == '.') n--;
    return n == want_len && strncasecmp(want, (const char *)id, n) == 0;
}

// Search a directory extent for one component; fills lba/size/is_dir. 0 if found.
static int find_in_dir(BlockDev *d, uint32_t dir_lba, uint32_t dir_size, const char *name,
                       size_t name_len, uint32_t *lba, uint32_t *size, int *is_dir) {
    if (dir_size == 0 || dir_size > ISO_MAX_DIR) return -1;
    uint32_t sectors = (dir_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint8_t *buf = malloc((size_t)sectors * SECTOR_SIZE);
    if (!buf) return -1;
    int found = -1;
    if (blockdev_read(d, dir_lba, sectors, buf) == 0) {
        for (uint32_t s = 0; s < sectors && found != 0; s++) {
            uint32_t pos = 0;
            const uint8_t *sec = buf + (size_t)s * SECTOR_SIZE;
            while (pos < SECTOR_SIZE) {
                uint8_t len = sec[pos];
                if (len == 0) break;                       // rest of sector is padding
                if (len < 34 || pos + len > SECTOR_SIZE) break;
                const uint8_t *r = sec + pos;
                uint8_t id_len = r[32];
                if (33u + id_len <= len && name_matches(name, name_len, r + 33, id_len)) {
                    *lba = rd32(r + 2);
                    *size = rd32(r + 10);
                    *is_dir = (r[25] & 2) != 0;
                    found = 0;
                    break;
                }
                pos += len;
            }
        }
    }
    free(buf);
    return found;
}

// Resolve a file path to its extent. 0 if it exists and is a regular file.
static int iso_lookup(BlockDev *d, const char *path, uint32_t *lba_out, uint32_t *len_out) {
    uint8_t pvd[SECTOR_SIZE];
    if (!d || blockdev_read(d, 16, 1, pvd) != 0) return -1;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return -1;
    uint32_t lba = rd32(pvd + 156 + 2), len = rd32(pvd + 156 + 10);
    int is_dir = 1;
    const char *p = path;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (!is_dir || find_in_dir(d, lba, len, p, n, &lba, &len, &is_dir) != 0) return -1;
        p += n;
        if (*p == '/') p++;
    }
    if (is_dir) return -1;
    *lba_out = lba;
    *len_out = len;
    return 0;
}

int iso_stat(BlockDev *d, const char *path, uint32_t *size) {
    uint32_t lba;
    return iso_lookup(d, path, &lba, size);
}

int iso_read_prefix(BlockDev *d, const char *path, uint8_t *buf, size_t n) {
    uint32_t lba, len;
    uint8_t sector[SECTOR_SIZE];
    if (iso_lookup(d, path, &lba, &len) != 0 || len == 0) return -1;
    if (blockdev_read(d, lba, 1, sector) != 0) return -1;
    size_t take = n < len ? n : len;
    if (take > SECTOR_SIZE) take = SECTOR_SIZE;
    memcpy(buf, sector, take);
    return (int)take;
}

uint8_t *iso_read_file(BlockDev *d, const char *path, size_t *size) {
    uint32_t lba, len;
    if (iso_lookup(d, path, &lba, &len) != 0 || len == 0 || len > ISO_MAX_FILE) return NULL;
    uint32_t sectors = (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint8_t *buf = malloc((size_t)sectors * SECTOR_SIZE);
    if (!buf) return NULL;
    if (blockdev_read(d, lba, sectors, buf) != 0) { free(buf); return NULL; }
    *size = len;
    return buf;
}
