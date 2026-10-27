#ifndef NEMO_SJIS_H
#define NEMO_SJIS_H

#include <stddef.h>

// The PSP lists file and folder names in Shift-JIS, its system code page: Cyrillic arrives as 0x84 xx
// pairs ("Эхо" = 84 5E 84 87 84 80). Paths stay raw for opening files; names are converted for the screen,
// for .m3u files and for names Nemo creates on the card (1.2.1).

// Shift-JIS name -> UTF-8 (Cyrillic and ASCII; other double-byte characters -> '?'). A name that already is
// valid UTF-8 is copied unchanged. Always NUL-terminates when cap > 0; never splits a character.
void sjis_to_utf8(const char *in, char *out, size_t cap);
// UTF-8 -> Shift-JIS (ASCII and Cyrillic; anything else -> '_'), for paths Nemo creates or looks up.
void utf8_to_sjis(const char *in, char *out, size_t cap);

#endif
