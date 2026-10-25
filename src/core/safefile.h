#ifndef NEMO_SAFEFILE_H
#define NEMO_SAFEFILE_H

#include <stddef.h>

// Crash-safe text files on a memory stick whose rename() cannot overwrite.
// Write order: <path>.tmp is written fully, <path> -> <path>.bak, <path>.tmp -> <path>, .bak removed.
// Reading first finishes or rolls back an interrupted write, so one intact copy always survives.

// Returns a malloc'd NUL-terminated copy, or NULL. *missing is 1 only when the file truly does
// not exist (no recoverable .tmp/.bak either); any other failure (unreadable, larger than
// max_size) leaves *missing at 0 so callers never mistake an error for "no file".
char *safe_read_text(const char *path, size_t max_size, int *missing);

// Replace the file contents. 0 on success, -1 on failure (the previous file is kept).
int safe_write_text(const char *path, const char *text);

#endif
