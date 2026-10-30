#include "vformat.h"
#include <string.h>
#include <strings.h>

const char *vformat_codec_name(const char *fourcc) {
    static const struct { const char *ids; const char *name; } MAP[] = {
        { "MJPG,jpeg,mjpa,mjpb,AVDJ", "Motion JPEG" },
        { "avc1,avc3,H264,h264,X264,x264,AVC1", "H.264" },
        { "hvc1,hev1,HEVC,hevc,H265", "H.265" },
        { "XVID,xvid", "XviD (MPEG-4)" },
        { "DIVX,divx,DX50,dx50,DIV3,div3", "DivX (MPEG-4)" },
        { "FMP4,fmp4,mp4v,MP4V,M4S2", "MPEG-4" },
        { "WMV1,WMV2,WMV3,WVC1", "Windows Media" },
        { "VP80,vp08", "VP8" },
        { "VP90,vp09", "VP9" },
        { "mpg1,MPG1,mpg2,MPG2,mp2v", "MPEG-1/2" },
    };
    if (!fourcc || !*fourcc) return "?";
    size_t n = strlen(fourcc);
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        for (const char *p = MAP[i].ids; *p;) {
            const char *e = strchr(p, ',');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len == n && !strncmp(p, fourcc, n)) return MAP[i].name;
            p += len + (e ? 1 : 0);
        }
    return fourcc;
}

static const char *ext(const char *path) {
    const char *d = strrchr(path, '.');
    return d ? d + 1 : "";
}

const char *vformat_container(const char *path) {
    static const char *const KNOWN[][2] = { { "avi", "AVI" }, { "mp4", "MP4" }, { "m4v", "MP4" }, { "mov", "MOV" },
                                            { "3gp", "3GP" }, { "mkv", "MKV" }, { "wmv", "WMV" }, { "flv", "FLV" },
                                            { "webm", "WebM" }, { "mpg", "MPEG" }, { "mpeg", "MPEG" } };
    const char *e = ext(path);
    for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0]; i++)
        if (!strcasecmp(e, KNOWN[i][0])) return KNOWN[i][1];
    return "";
}

int vformat_is_video(const char *path) { return vformat_container(path)[0] != '\0'; }

int vformat_is_bmff(const char *path) {
    const char *e = ext(path);
    return !strcasecmp(e, "mp4") || !strcasecmp(e, "m4v") || !strcasecmp(e, "mov") || !strcasecmp(e, "3gp");
}
