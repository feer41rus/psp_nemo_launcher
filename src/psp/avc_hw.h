#ifndef NEMO_AVC_HW_H
#define NEMO_AVC_HW_H

#include <stdint.h>

// H.264 in MP4 through the XMB's decoder path (1.3.3). One session at a time. Pictures come out as RGBA,
// 512 per row (up to 480x272).
int  avc_open(const uint8_t *avcc, int avcc_len, char *why, int cap);   // 0 ok
// One MP4 sample in decode order; 0 ok, else the error. The decoder may write its pictures straight into
// out[0..n_out) (*direct = 1: *pictures of them, in display order); otherwise avc_picture() gives the last one.
int  avc_feed(uint8_t *nal, int size, uint32_t **out, int n_out, int *pictures, int *direct);
int  avc_picture(int index, uint32_t *rgb);             // picture index (0..pictures-1, display order) into rgb; 0 ok
void avc_restart(void);                                 // after a seek: the next sample is a fresh start
void avc_close(void);
void avc_dump_detail(int pics);                         // diagnostics: Detail2 raw into the log
int  avc_media_load(char *why, int cap);                // avcodec, mpegbase, mpeg_vsh only (UMD Video); 0 ok
void avc_dump_detail(int pics);                         // diagnostics: Detail2 raw into the log                                   // the Media Engine goes back to the games' mode
int  avc_x264_pyramid(const uint8_t *sample, int size); // x264 says "b_pyramid" (the PSP cannot play it)

#endif
