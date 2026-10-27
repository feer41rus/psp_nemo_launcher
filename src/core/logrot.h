#ifndef NEMO_LOGROT_H
#define NEMO_LOGROT_H

#include <stddef.h>

// How many runs of nemo.log to keep (Settings › System › Debug › Keep logs, 1.2.0). 0 = no log at all.
#define LOG_KEEP_DEFAULT 2
#define LOG_KEEP_MAX 20
#define LOG_KEEP_CHOICE_COUNT 5
extern const int LOG_KEEP_CHOICES[LOG_KEEP_CHOICE_COUNT];   // {0, 2, 5, 10, 20}

int  logrot_valid_keep(int keep);   // keep if it is one of LOG_KEEP_CHOICES, else LOG_KEEP_DEFAULT
// i = 0 -> "dir/nemo.log", i >= 1 -> "dir/nemo.<i>.log".
void logrot_path(char *out, size_t cap, const char *dir, int i);
// Before a new run: nemo.log -> nemo.1.log -> ... keeping keep-1 old runs; older files up to
// nemo.<LOG_KEEP_MAX>.log and the pre-1.2.0 nemo.prev.log are removed. keep == 0 touches nothing.
void logrot_rotate(const char *dir, int keep);

#endif
