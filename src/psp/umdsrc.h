#ifndef NEMO_UMDSRC_H
#define NEMO_UMDSRC_H

#include <stdint.h>

// UMD Music (1.3.5): one stream of the disc (UMD_AUDIO/STREAM/*.MPS, MPEG-PS with ATRAC3plus) as 44.1 kHz stereo
// PCM, through sceMpeg's demuxer — the same way flacsrc gives FLAC to flacplay.
typedef struct UmdSrc UmdSrc;

int      umdsrc_is_path(const char *path);                 // a stream on a UMD Music disc
UmdSrc  *umdsrc_open(const char *path, uint32_t start_ms);  // NULL: unreadable or no sound
uint32_t umdsrc_read(UmdSrc *s, int16_t *out, uint32_t frames);   // 0 at the end
uint32_t umdsrc_duration_ms(const UmdSrc *s);
void     umdsrc_close(UmdSrc *s);

#endif
