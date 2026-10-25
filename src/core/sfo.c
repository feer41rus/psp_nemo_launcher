#include "sfo.h"
#include "utf8.h"
#include <string.h>

#define SFO_FMT_UTF8   0x0204
#define SFO_FMT_UTF8S  0x0004
#define SFO_FMT_INT32  0x0404

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// Locate an entry; returns pointer to its value and fills fmt/len, or NULL.
static const uint8_t *find(const uint8_t *sfo, size_t size, const char *key,
                           uint16_t *fmt, uint32_t *len) {
    if (!sfo || size < 20 || memcmp(sfo, "\0PSF", 4) != 0) return NULL;
    uint32_t key_start = rd32(sfo + 8), data_start = rd32(sfo + 12), count = rd32(sfo + 16);
    if (key_start > size || data_start > size || count > 1024 || 20 + (size_t)count * 16 > size)
        return NULL;
    size_t key_len = strlen(key);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = sfo + 20 + i * 16;
        size_t ko = key_start + rd16(e);
        if (ko + key_len + 1 > size) continue;
        if (memcmp(sfo + ko, key, key_len + 1) != 0) continue;
        uint32_t l = rd32(e + 4);
        size_t doff = data_start + (size_t)rd32(e + 12);
        if (doff > size || l > size - doff) return NULL;
        *fmt = rd16(e + 2);
        *len = l;
        return sfo + doff;
    }
    return NULL;
}

int sfo_get_string(const uint8_t *sfo, size_t size, const char *key, char *out, size_t cap) {
    uint16_t fmt;
    uint32_t len;
    const uint8_t *v = find(sfo, size, key, &fmt, &len);
    if (!v || (fmt != SFO_FMT_UTF8 && fmt != SFO_FMT_UTF8S) || cap == 0) return -1;
    char tmp[512];
    size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, v, n);
    tmp[n] = '\0';
    utf8_copy(out, cap, tmp);
    return 0;
}

int sfo_get_u32(const uint8_t *sfo, size_t size, const char *key, uint32_t *out) {
    uint16_t fmt;
    uint32_t len;
    const uint8_t *v = find(sfo, size, key, &fmt, &len);
    if (!v || fmt != SFO_FMT_INT32 || len < 4) return -1;
    *out = rd32(v);
    return 0;
}
