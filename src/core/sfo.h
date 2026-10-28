#ifndef NEMO_SFO_H
#define NEMO_SFO_H

#include <stddef.h>
#include <stdint.h>

// Read a UTF-8 string value from a PARAM.SFO buffer. 0 on success, -1 if missing/corrupt.
int sfo_get_string(const uint8_t *sfo, size_t size, const char *key, char *out, size_t cap);

// Read an int32 value from a PARAM.SFO buffer. 0 on success, -1 if missing/corrupt.
int sfo_get_u32(const uint8_t *sfo, size_t size, const char *key, uint32_t *out);

// Overwrite a string value in place (it must fit the entry's max length). 0 ok, -1 missing / too long.
int sfo_set_string(uint8_t *sfo, size_t size, const char *key, const char *value);

#endif
