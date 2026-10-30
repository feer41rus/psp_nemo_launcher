#ifndef NEMO_TEXTDOC_H
#define NEMO_TEXTDOC_H

#include <stddef.h>

// The text editor's document (1.3.2): lines in memory, written back exactly as they came
// (each line's own ending, BOM, a final newline or not), only edited lines differ. New lines take
// the ending of the first line (crlf).
#define TEXTDOC_MAX_BYTES (256 * 1024)

typedef struct {
    char **lines;
    unsigned char *changed;   // per line: edited since loading
    unsigned char *eol;       // per line: 0 none (the last line only), 1 "\n", 2 "\r\n"
    int count, cap;
    int crlf, bom, final_nl;  // as loaded: the first line's ending, a BOM, a newline at the end
    int dirty;
} TextDoc;

int  textdoc_parse(TextDoc *d, const char *data, size_t n);   // -1: a NUL byte (not text) or no memory
char *textdoc_join(const TextDoc *d, size_t *n);             // malloc'd, NUL-terminated; NULL no memory
int  textdoc_set(TextDoc *d, int line, const char *text);
int  textdoc_insert(TextDoc *d, int after, const char *text);   // a new line below `after`
int  textdoc_delete(TextDoc *d, int line);                      // the only line becomes empty
int  textdoc_find(const TextDoc *d, int from, const char *needle);   // first line >= from with it, wrapping; -1
void textdoc_free(TextDoc *d);

#endif
