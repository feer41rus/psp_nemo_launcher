#ifndef NEMO_ISO9660_H
#define NEMO_ISO9660_H

#include <stddef.h>
#include <stdint.h>
#include "blockdev.h"

// Read a file such as "PSP_GAME/PARAM.SFO" (case-insensitive) into a malloc'd buffer.
// Returns NULL if the image is not ISO9660, the file is missing, or it is too large.
uint8_t *iso_read_file(BlockDev *d, const char *path, size_t *size);

// Size of a regular file inside the image. 0 if found, -1 otherwise.
int iso_stat(BlockDev *d, const char *path, uint32_t *size);

// Read up to n bytes (max one sector) from the start of a file. Returns bytes read or -1.
int iso_read_prefix(BlockDev *d, const char *path, uint8_t *buf, size_t n);

#endif
