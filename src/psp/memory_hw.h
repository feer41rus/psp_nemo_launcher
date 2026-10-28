#ifndef NEMO_MEMORY_HW_H
#define NEMO_MEMORY_HW_H

#include "memstat.h"
#include <stddef.h>
#include <stdint.h>

// "About PSP › Memory" (1.2.6): reading the card, flash, RAM and UMD; the category count runs in its
// own low-priority thread, only when asked (button), and ends when done or stopped.
typedef struct { int ok; long long total, free; uint32_t cluster, clusters; } MemCard;

int  memory_card_read(const char *dev, MemCard *c);   // "ms0:", "flash0:"...; 0 ok
int  memory_card_boot(MemFs *fs, char label[12]);     // the card's boot sector; 0 ok, -1 unreadable
long long memory_umd_size(void);                      // game disc image size, -1 if none / unreadable

int  memory_scan_busy(void);    // a count (of either storage) is running or stopping
void memory_scan_start(const char *dev);   // "ms0:" or "ef0:"; no-op while a count is running
void memory_scan_stop(void);    // the thread ends at its next entry; nobody waits for it
// 0 = counting (out: totals so far), 1 = done (out: the last full count), -1 = never counted.
// `cur`: the folder being counted ("" when idle).
int  memory_scan_state(const char *dev, MemTotals *out, char *cur, size_t cap);

#endif
