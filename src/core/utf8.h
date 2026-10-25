#ifndef NEMO_UTF8_H
#define NEMO_UTF8_H

#include <stddef.h>

// Copy src into dst (capacity cap), truncating only at a codepoint boundary.
// Always NUL-terminates when cap > 0. Returns the resulting byte length.
size_t utf8_copy(char *dst, size_t cap, const char *src);

// Byte index of the codepoint that starts before position pos (0 if none).
size_t utf8_prev(const char *s, size_t pos);

// Case-insensitive comparison of UTF-8 strings: folds ASCII and Cyrillic (incl. Ё/ё).
int utf8_casecmp(const char *a, const char *b);

#endif
