#ifndef NEMO_CAMFILE_H
#define NEMO_CAMFILE_H

#include <stddef.h>

// Camera file names by date and time (1.3.6): <dir>/IMG_20261030_140305.JPG, "_2", "_3"… when taken.
typedef struct { int year, month, day, hour, minute, second; } CamTime;

int camfile_name(char *out, size_t cap, const char *dir, const char *prefix, const char *ext, const CamTime *t,
                 int (*exists)(const char *path));   // 0 ok, -1 does not fit / no free name

#endif
