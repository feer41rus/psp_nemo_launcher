#ifndef NEMO_PAD_H
#define NEMO_PAD_H

#include <stdint.h>

void pad_init(void);
// Current buttons (NB_* compatible, includes HOME and NOTE when kernel read works) and stick.
uint32_t pad_read(uint8_t *lx, uint8_t *ly);

#endif
