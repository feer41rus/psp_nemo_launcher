// UMD Music streams through sceMpeg (1.3.5): a ring buffer filled from the disc by the library's own callback,
// GetAtracAu/AtracDecode for the ATRAC3plus sound (2048 stereo frames at 44100 Hz each). No picture stream is
// registered. Written without a disc: everything it meets goes to the log (author: keep all diagnostics).
#include "umdsrc.h"
#include "avc_hw.h"
#include "log.h"
#include "mps.h"
#include "platform.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pspkernel.h>
#include <pspmpeg.h>

#define PACK 2048
#define PACKETS 64     // 128 KB of the disc ahead: seconds of ATRAC3plus
#define AU_FRAMES 2048

struct UmdSrc {
    FILE *f;
    SceMpeg mpeg;
    SceMpegRingbuffer rb;
    SceMpegStream *as;
    SceMpegAu *au;
    void *mbuf, *rbuf, *es;
    int created, rb_made, r_init;
    int eof, first, errors, misses, ch;
    long long packs;
    uint32_t duration_ms, decoded;
    int16_t pcm[AU_FRAMES * 2] __attribute__((aligned(64)));
    int pcm_pos, pcm_left;
    char path[160];
};

int umdsrc_is_path(const char *path) { return !strncasecmp(path, "disc0:/UMD_AUDIO/", 17); }

static SceInt32 fill(ScePVoid data, SceInt32 packets, ScePVoid param) {
    UmdSrc *s = param;
    uint32_t t0 = platform_ms();
    int n = (int)fread(data, PACK, (size_t)packets, s->f);
    uint32_t dt = platform_ms() - t0;
    if (dt > 300) log_msg("umdmusic: disc read of %d packs took %u ms", (int)packets, (unsigned)dt);
    if (n <= 0) {
        if (!s->eof) log_dbg("umdmusic: end of %s", s->path);
        s->eof = 1;
    }
    return n > 0 ? n : 0;
}

// The length and the sound track from the start and the end of the stream.
static void probe(UmdSrc *s) {
    uint8_t *b = malloc(256 * 1024);
    if (!b) return;
    fseek(s->f, 0, SEEK_END);
    long size = ftell(s->f);
    s->packs = size / PACK;
    fseek(s->f, 0, SEEK_SET);
    int n = (int)fread(b, 1, 256 * 1024, s->f);
    int64_t first = 0, last = 0;
    int r1 = mps_audio_first_pts(b, n, &first);
    int mask = mps_audio_tracks(b, n);
    {   // the first bytes, for a disc layout we never saw
        char hex[3 * 32 + 1];
        for (int i = 0; i < 32 && i < n; i++) snprintf(hex + 3 * i, 4, "%02x ", b[i]);
        log_msg("umdmusic: %s: %ld bytes, head %s", s->path, size, n > 0 ? hex : "-");
    }
    fseek(s->f, size > 256 * 1024 ? size - 256 * 1024 : 0, SEEK_SET);
    n = (int)fread(b, 1, 256 * 1024, s->f);
    int r2 = mps_audio_last_pts(b, n, &last);
    free(b);
    s->duration_ms = (uint32_t)mps_duration_ms(first, last);
    s->ch = 0;
    while (s->ch < 15 && mask && !(mask & (1 << s->ch))) s->ch++;
    log_msg("umdmusic: pts %lld..%lld (rc %d %d) = %u ms, sound tracks %#x -> %d, video pts %s", (long long)first,
            (long long)last, r1, r2, (unsigned)s->duration_ms, mask, s->ch, "not used");
}

static int build(UmdSrc *s, uint32_t start_ms) {
    long long at = s->duration_ms > 0 && start_ms < s->duration_ms ? s->packs * start_ms / s->duration_ms : 0;
    if (fseek(s->f, (long)(at * PACK), SEEK_SET) != 0) return -1;
    s->r_init = sceMpegInit();
    int rsize = sceMpegRingbufferQueryMemSize(PACKETS);
    s->rbuf = rsize > 0 ? memalign(64, (size_t)rsize) : NULL;
    int msize = sceMpegQueryMemSize(0);
    s->mbuf = msize > 0 ? memalign(64, (size_t)msize) : NULL;
    s->au = memalign(64, 64);
    if (!s->rbuf || !s->mbuf || !s->au) { log_fail("umdmusic: no memory (ring %d, mpeg %d)", rsize, msize); return -2; }
    memset(s->au, 0, 64);
    int r1 = sceMpegRingbufferConstruct(&s->rb, PACKETS, s->rbuf, rsize, fill, s);
    s->rb_made = r1 == 0;
    int r2 = sceMpegCreate(&s->mpeg, s->mbuf, msize, &s->rb, 512, 0, 0);
    s->created = r2 == 0;
    SceInt32 es_size = 0, out_size = 0;
    if (s->created) {
        s->as = sceMpegRegistStream(&s->mpeg, 1, s->ch);   // ATRAC3plus only: no picture
        sceMpegQueryAtracEsSize(&s->mpeg, &es_size, &out_size);
        s->es = es_size > 0 ? memalign(64, (size_t)es_size) : NULL;
    }
    int r3 = s->es ? sceMpegInitAu(&s->mpeg, s->es, s->au) : -1;
    log_msg("umdmusic: from pack %lld of %lld (%u ms): init %08x ring %d B %08x, mpeg %d B %08x, stream %p, es %d/%d %08x",
            at, s->packs, (unsigned)start_ms, (unsigned)s->r_init, rsize, (unsigned)r1, msize, (unsigned)r2, (void *)s->as,
            (int)es_size, (int)out_size, (unsigned)r3);
    if (!s->rb_made || !s->created || !s->as || r3 != 0) return -3;
    s->first = 1;
    return 0;
}

UmdSrc *umdsrc_open(const char *path, uint32_t start_ms) {
    uint32_t t0 = platform_ms();
    char err[96];
    if (avc_media_load(err, sizeof err) != 0) { log_fail("umdmusic: modules: %s", err); return NULL; }
    UmdSrc *s = memalign(64, sizeof *s);
    if (!s) return NULL;
    memset(s, 0, sizeof *s);
    s->r_init = -1;
    snprintf(s->path, sizeof s->path, "%s", path);
    s->f = fopen(path, "rb");
    if (!s->f) { log_fail("umdmusic: cannot open %s", path); umdsrc_close(s); return NULL; }
    probe(s);
    if (build(s, start_ms) != 0) { log_fail("umdmusic: decoder for %s", path); umdsrc_close(s); return NULL; }
    log_msg("umdmusic: open %s at %u ms in %u ms", path, (unsigned)start_ms, (unsigned)(platform_ms() - t0));
    return s;
}

// One access unit decoded into s->pcm; 0 at the end of the stream.
static int decode_one(UmdSrc *s) {
    for (;;) {
        int avail = sceMpegRingbufferAvailableSize(&s->rb);
        if (avail > 0 && !s->eof) sceMpegRingbufferPut(&s->rb, avail > 16 ? 16 : avail, avail);
        int rc = sceMpegGetAtracAu(&s->mpeg, s->as, s->au, NULL);
        if (rc == 0) rc = sceMpegAtracDecode(&s->mpeg, s->au, s->pcm, s->first);
        if (rc == 0) {
            if (s->first) log_msg("umdmusic: first sound, pts %u", (unsigned)s->au->iPts);
            s->first = 0;
            s->misses = 0;
            s->decoded++;
            sceKernelDcacheWritebackInvalidateRange(s->pcm, sizeof s->pcm);
            s->pcm_pos = 0;
            s->pcm_left = AU_FRAMES;
            return 1;
        }
        if (rc != (int)0x80618001 && s->errors++ < 5) log_msg("umdmusic: sound rc %08x after %u units", rc, (unsigned)s->decoded);
        if (s->eof && ++s->misses > 8) {
            log_msg("umdmusic: stream over after %u units (%u errors)", (unsigned)s->decoded, (unsigned)s->errors);
            return 0;
        }
        if (s->errors > 200) { log_fail("umdmusic: too many errors in %s", s->path); return 0; }
        sceKernelDelayThread(2000);
    }
}

uint32_t umdsrc_read(UmdSrc *s, int16_t *out, uint32_t frames) {
    uint32_t done = 0;
    while (done < frames) {
        if (!s->pcm_left && !decode_one(s)) break;
        uint32_t n = frames - done < (uint32_t)s->pcm_left ? frames - done : (uint32_t)s->pcm_left;
        memcpy(out + done * 2, s->pcm + s->pcm_pos * 2, n * 4);
        s->pcm_pos += (int)n;
        s->pcm_left -= (int)n;
        done += n;
    }
    return done;
}

uint32_t umdsrc_duration_ms(const UmdSrc *s) { return s->duration_ms; }

void umdsrc_close(UmdSrc *s) {
    if (!s) return;
    if (s->created) {
        if (s->as) sceMpegUnRegistStream(&s->mpeg, s->as);
        sceMpegDelete(&s->mpeg);
    }
    if (s->rb_made) sceMpegRingbufferDestruct(&s->rb);
    // sceMpegFinish is left alone: the video player may use the library too (its own Init/Finish pair)
    if (s->f) fclose(s->f);
    free(s->mbuf);
    free(s->rbuf);
    free(s->es);
    free(s->au);
    log_dbg("umdmusic: closed %s (%u units)", s->path, (unsigned)s->decoded);
    free(s);
}
