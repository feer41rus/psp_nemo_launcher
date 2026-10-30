#ifndef NEMO_VFORMAT_H
#define NEMO_VFORMAT_H

// Video formats for people (1.3.3): "XviD (MPEG-4)" instead of "XVID", the container from the name.
const char *vformat_codec_name(const char *fourcc);
const char *vformat_container(const char *path);   // "AVI", "MP4", "MOV", "MKV"...; "" not a video
int vformat_is_video(const char *path);             // listed in Video (playable or not: the reason is told)
int vformat_is_bmff(const char *path);              // MP4 / M4V / MOV / 3GP: read by mp4.c

#endif
