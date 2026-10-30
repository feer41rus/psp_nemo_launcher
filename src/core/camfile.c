#include "camfile.h"
#include <stdio.h>

int camfile_name(char *out, size_t cap, const char *dir, const char *prefix, const char *ext, const CamTime *t,
                 int (*exists)(const char *path)) {
    for (int n = 1; n < 1000; n++) {
        char sfx[8] = "";
        if (n > 1) snprintf(sfx, sizeof sfx, "_%d", n);
        int len = snprintf(out, cap, "%s/%s_%04d%02d%02d_%02d%02d%02d%s.%s", dir, prefix, t->year, t->month, t->day, t->hour,
                           t->minute, t->second, sfx, ext);
        if (len < 0 || (size_t)len >= cap) return -1;
        if (!exists || !exists(out)) return 0;
    }
    return -1;
}
