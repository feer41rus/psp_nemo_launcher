#ifndef NEMO_BT_HW_H
#define NEMO_BT_HW_H

#include "btreg.h"

// Bluetooth on the PSP Go (1.3.12): shown only, read from the system registry (see src/core/btreg.h).
void bt_refresh(BtState *out, int log_all);   // walks /CONFIG; log_all: every Bluetooth-looking entry, the
                                              // registry's categories and the loaded modules (detailed log)
void bt_log_state(const char *why, const BtState *s);

#endif
