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

// Codepoints in s (FAT names allow 255 of them, whatever their bytes).
size_t utf8_len(const char *s);

// Bytes of the codepoint at s (0 at the end of the string).
size_t utf8_next(const char *s);

// Byte index of the first case-insensitive match of needle in hay (folding as above), -1 if none or empty.
int utf8_casefind(const char *hay, const char *needle);

#endif
