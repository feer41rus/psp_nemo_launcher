#ifndef NEMO_MP3INFO_H
#define NEMO_MP3INFO_H

#include <stddef.h>
#include <stdint.h>

// MP3 header facts for the hardware decoder: where the audio is, how long it is, where to start
// for a given time — all from the first few KB (no whole-file scan).
typedef struct {
    uint32_t audio_start, audio_end;   // bytes: first frame .. end of audio (ID3v1 excluded)
    int mpeg;                          // 10 = MPEG1, 20 = MPEG2, 25 = MPEG2.5
    int sample_rate, channels, samples_per_frame, bitrate_kbps;
    uint32_t frames;                   // Xing/Info/VBRI frame count, 0 = unknown (CBR estimate)
    uint32_t stream_bytes;             // Xing/VBRI byte count, 0 = unknown
    int has_toc;
    uint8_t toc[100];
    int enc_delay, enc_padding;        // LAME tag, 0 if absent
    int info_frame;                    // first frame is a Xing/Info/VBRI frame (no audio)
    int first_frame_len;               // bytes of the first frame (skip it when info_frame)
} Mp3Info;

uint32_t mp3info_id3v2_size(const uint8_t *b, size_t len);   // bytes to skip at file start (0 if none)
// head = bytes of the file from audio_start. 0 ok, -1 not a supported MPEG Layer III stream.
int mp3info_parse(const uint8_t *head, size_t len, uint32_t audio_start, uint32_t file_size, int has_id3v1,
                  Mp3Info *out);
// Offset of the first frame header confirmed by the next one (-1 if none in b).
int mp3info_find_frame(const uint8_t *b, size_t len);
uint32_t mp3info_duration_ms(const Mp3Info *m);
uint32_t mp3info_offset_for_ms(const Mp3Info *m, uint32_t ms);   // >= audio_start, < audio_end

#endif
