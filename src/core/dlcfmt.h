#ifndef NEMO_DLCFMT_H
#define NEMO_DLCFMT_H

#include <stddef.h>
#include <stdint.h>

// DLC file formats (1.2.5): the PARAM.SFO inside a DLC's PARAM.PBP, title matching, encrypted files.
int dlc_pbp_sfo(const uint8_t *pbp, size_t size, size_t *off, size_t *len);   // section 0; 0 / -1
int dlc_title_eq(const char *a, const char *b);   // case, edge spaces, ™ and ® ignored; empty never equal
// 1 = encrypted ("\0PSPEDAT" or "NPD\0"), cid = its Content ID (36 chars); 0 = plain data.
int dlc_encrypted(const uint8_t *head, size_t n, char cid[37]);

#endif
