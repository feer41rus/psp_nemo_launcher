#ifndef NEMO_PBP_H
#define NEMO_PBP_H

#include <stdint.h>
#include <stdio.h>

enum { PBP_PARAM_SFO, PBP_ICON0, PBP_ICON1, PBP_PIC0, PBP_PIC1, PBP_SND0, PBP_DATA_PSP, PBP_DATA_PSAR };

typedef struct {
    uint32_t off[8];
    uint32_t file_size;
} PbpHeader;

// Read and validate the PBP header. 0 on success, -1 if not a valid PBP.
int pbp_read_header(FILE *f, PbpHeader *h);

// Read one section into a malloc'd buffer; NULL if empty or unreadable.
uint8_t *pbp_read_section(FILE *f, const PbpHeader *h, int section, size_t *size);

#endif
