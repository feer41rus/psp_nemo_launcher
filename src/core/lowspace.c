#include "lowspace.h"

#define MB (1024LL * 1024)
#define LOW_BYTES (1024 * MB)
#define LOW_PERCENT 10
#define CRIT_BYTES (200 * MB)

LowSpace lowspace_level(long long free_bytes, long long total_bytes) {
    if (free_bytes < 0 || total_bytes <= 0) return LOWSPACE_OK;
    if (free_bytes < CRIT_BYTES) return LOWSPACE_CRIT;
    if (free_bytes < LOW_BYTES || free_bytes * 100 < total_bytes * LOW_PERCENT) return LOWSPACE_LOW;
    return LOWSPACE_OK;
}

LowSpace lowspace_worst(LowSpace a, LowSpace b) { return a > b ? a : b; }
