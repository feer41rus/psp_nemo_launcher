#ifndef NEMO_BLOCKDEV_H
#define NEMO_BLOCKDEV_H

#include <stdint.h>

#define SECTOR_SIZE 2048

// Sector reader over a plain ISO or a CSO (v1, 2048-byte blocks).
typedef struct BlockDev BlockDev;

BlockDev *blockdev_open(const char *path);   // NULL if missing or not a valid ISO/CSO container
int blockdev_read(BlockDev *d, uint32_t lba, uint32_t count, uint8_t *out); // 0 ok, -1 error
uint32_t blockdev_sectors(BlockDev *d);
void blockdev_close(BlockDev *d);

#endif
