// Video playback (1.3.3): camera AVI (Motion JPEG through sceJpeg, PCM sound) and MP4 (H.264 through the
// XMB's decoder path, avc_hw.c; AAC through sceAudiocodec). Two threads: pictures (into three RGBA buffers,
// shown when their time comes, late ones skipped) and sound (resampled to 44100 Hz stereo on a channel of
// its own; the frames it has played are the clock). Without sound the wall clock leads.
#include "video_hw.h"
#include "avc_hw.h"
#include "adpcm.h"
#include "avi.h"
#include "i18n.h"
#include "log.h"
#include "mp4.h"
#include "mps.h"
#include "pcmrs.h"
#include "vformat.h"
#include "platform.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pspaudio.h>
#include <pspaudiocodec.h>
#include <pspjpeg.h>
#include <pspmpeg.h>
#include <pspkernel.h>
#include <psputility.h>
#include <turbojpeg.h>
#include <sys/stat.h>

#define W 480
#define H 272
#define NBUF 3
#define NBUF_MAX 7        // MP4: 3 for showing + 4 the decoder may fill in one call
#define OUT_LIST 4
#define JPEG_MAX (256 * 1024)
#define SOFT_W 256      // software Motion JPEG: the largest picture decoded
#define SOFT_H 192
#define OUT_FRAMES 1024
#define LATE_MS 100

#define STRIDE_MP4 512
#define NAL_MAX (512 * 1024)

typedef enum { VK_NONE, VK_AVI, VK_MP4, VK_PSMF } VideoKind;

static VideoKind kind;
static Avi avi;
static Mp4 mp4;
static int vtr = -1, atr = -1, ttr = -1;   // MP4 tracks
static uint8_t *nal;                      // one H.264 sample
static unsigned long aac[65] __attribute__((aligned(64)));
static int aac_edram, aac_ok;
static int audio_be;    // MOV 'twos': 16-bit big-endian sound, swapped on reading
static int mov_aac;     // MOV with Motion JPEG and AAC: pictures the AVI way, sound the MP4 way
static VideoInfo vinfo;
static char vpath[512];
static FILE *vf, *af;
static uint32_t *bufs[NBUF_MAX];
static int nbufs;
static uint8_t *jbuf;
static tjhandle soft;   // Motion JPEG the hardware refuses (4:2:2, larger than the screen): libjpeg-turbo, scaled down
static int soft_w, soft_h;
static volatile int ready = -1, reading = -1, ready_seq, taken_seq;
static volatile int quit, paused, ended, opened;
static volatile int gen;                 // bumped by a seek: the picture thread starts over from seek_ms
static volatile int agen;                // bumped by a seek or a resume: the sound starts over from seek_ms (review I9)
static int hw_frames = OUT_FRAMES;       // frames one sound buffer holds (the PSMF thread: 2048)
static volatile int stuck;               // threads did not end in time: their buffers are left alone (review I3)
static volatile int64_t seek_ms;
// the clock
static volatile int64_t base_ms;         // position when the clock (re)started
static volatile uint32_t audio_out;      // stereo frames played since then
static volatile uint32_t wall_start;     // platform_ms() when it started (no sound / sound ended)
static volatile int audio_on, audio_done;
static volatile int64_t paused_at;
static SceUID vth = -1, ath = -1;
static volatile int vth_alive, ath_alive;
static int decoded, dropped, worst;
static volatile int audio_gaps;   // output buffers handed over after the previous one had run out (log)

static int64_t clock_ms(void) {
    if (paused) return paused_at;
    if (audio_on && !audio_done) {
        int64_t played = (int64_t)audio_out - hw_frames;   // one buffer is still in the hardware
        if (played < 0) played = 0;
        return base_ms + played * 1000 / 44100;
    }
    return base_ms + (int64_t)(platform_ms() - wall_start);
}

int64_t video_pos_ms(void) {
    int64_t p = clock_ms();
    return p < 0 ? 0 : p > vinfo.duration_ms ? vinfo.duration_ms : p;
}

static void restart_clock(int64_t ms) {
    base_ms = ms;
    audio_out = 0;
    audio_done = 0;
    wall_start = platform_ms();
}

// ---------- pictures ----------

static int64_t frame_ms(int i) { return (int64_t)i * avi.us_per_frame / 1000; }

static int decode(int i) {
    const AviChunk *c = &avi.video[i];
    if (c->size <= 0 || c->size > JPEG_MAX) return -1;
    if (fseek(vf, (long)c->offset, SEEK_SET) != 0 || fread(jbuf, 1, (size_t)c->size, vf) != (size_t)c->size) return -1;
    int back = 0;
    while (back == ready || back == reading) back++;
    uint32_t t0 = platform_ms();
    int rc;
    if (soft) {
        rc = tjDecompress2(soft, jbuf, (unsigned long)c->size, (unsigned char *)bufs[back], soft_w, soft_w * 4, soft_h,
                           TJPF_RGBX, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE);
    } else {
        sceKernelDcacheWritebackAll();
        rc = sceJpegDecodeMJpeg(jbuf, (SceSize)c->size, (u8 *)bufs[back], 0);
        sceKernelDcacheWritebackInvalidateAll();
    }
    int dt = (int)(platform_ms() - t0);
    if (dt > worst) worst = dt;
    if (rc < 0) return -1;
    ready = back;
    ready_seq++;
    decoded++;
    return 0;
}

static int video_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int my_gen = -1, frame = 0, shown = 0;
    while (!quit) {
        if (my_gen != gen) {
            my_gen = gen;
            frame = avi_frame_at(&avi, seek_ms);
            shown = 0;
        }
        if (frame >= avi.n_video) {
            if (video_pos_ms() >= vinfo.duration_ms - 50 || !audio_on || audio_done) ended = 1;
            sceKernelDelayThread(10000);
            continue;
        }
        if (paused && shown) { sceKernelDelayThread(10000); continue; }   // after a seek one picture is shown
        int64_t now = clock_ms(), at = frame_ms(frame);
        if (shown && at > now + 5) {
            int64_t wait = at - now;
            sceKernelDelayThread((SceUInt)((wait > 20 ? 20 : wait) * 1000));
            continue;
        }
        if (shown && now - at > LATE_MS) {   // behind: jump to the picture of now
            int to = avi_frame_at(&avi, now);
            if (to > frame) { dropped += to - frame; frame = to; }
        }
        if (decode(frame) == 0) shown = 1;
        frame++;
    }
    vth_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

// ---------- pictures: MP4 / H.264 ----------

// Samples go in decode order; pictures come out in display order, so each takes the smallest pending time.
static int64_t pts_pop(int64_t *q, int *n) {
    if (*n <= 0) return 0;
    int k = 0;
    for (int i = 1; i < *n; i++) if (q[i] < q[k]) k = i;
    int64_t v = q[k];
    q[k] = q[--*n];
    return v;
}

typedef struct { int buf; int64_t pts; } Pic;

static int mp4_video_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const Mp4Track *t = &mp4.tracks[vtr];
    int my_gen = -1, i = 0, shown = 0, errors = 0, logged = 0;
    int64_t target = 0, q[32];
    int qn = 0;
    Pic pend[OUT_LIST];   // decoded, waiting for their time, in display order
    int np = 0;
    while (!quit) {
        if (my_gen != gen) {   // a seek: from the key frame before, pictures before the target not shown
            my_gen = gen;
            target = seek_ms;
            i = mp4_key_before(&mp4, vtr, target);
            avc_restart();
            qn = 0;
            np = 0;
            shown = 0;
        }
        if (np > 0) {
            Pic *p = &pend[0];
            int drop = 0;
            if (p->pts + 40 < target) drop = 1;   // still before the seek point
            else {
                if (paused && shown) { sceKernelDelayThread(10000); continue; }
                int64_t now = clock_ms();
                if (shown && p->pts > now + 5) {
                    int64_t wait = p->pts - now;
                    sceKernelDelayThread((SceUInt)((wait > 20 ? 20 : wait) * 1000));
                    continue;
                }
                if (shown && now - p->pts > LATE_MS) { dropped++; drop = 1; }   // late: decoded, not shown
            }
            if (!drop) {
                ready = p->buf;
                ready_seq++;
                shown = 1;
            }
            memmove(pend, pend + 1, (size_t)(--np) * sizeof *pend);
            continue;
        }
        if (i >= t->n_samples) {
            if (video_pos_ms() >= vinfo.duration_ms - 50 || !audio_on || audio_done) ended = 1;
            sceKernelDelayThread(10000);
            continue;
        }
        if (paused && shown) { sceKernelDelayThread(10000); continue; }
        Mp4Sample sm;
        if (mp4_sample(&mp4, vtr, i, &sm) != 0 || sm.size <= 0 || sm.size > NAL_MAX ||
            fseek(vf, (long)sm.offset, SEEK_SET) != 0 || fread(nal, 1, (size_t)sm.size, vf) != (size_t)sm.size) {
            i++;
            continue;
        }
        i++;
        if (qn >= 16) pts_pop(q, &qn);   // a stale time never piles up (review I8)
        q[qn++] = sm.pts_ms;
        // the list: buffers that are neither on screen nor being copied (the pending queue is empty here)
        uint32_t *outs[OUT_LIST];
        int ids[OUT_LIST], n_out = 0;
        for (int b = 0; b < nbufs && n_out < OUT_LIST; b++)
            if (b != ready && b != reading) { ids[n_out] = b; outs[n_out++] = bufs[b]; }
        int pics = 0, direct = 0;
        uint32_t t0 = platform_ms();
        int rc = avc_feed(nal, sm.size, outs, n_out, &pics, &direct);
        if (rc != 0) {
            qn--;   // this sample's time goes with it (review I8)
            if (errors++ < 3) log_msg("video: h264 sample %d rc %08x", i - 1, rc);
            continue;
        }
        if (pics <= 0) continue;
        if ((pics > 1 || i < 8) && logged++ < 6) log_msg("video: h264 %d picture(s) from sample %d, written by the decoder: %d", pics, i - 1, direct);
        static int dumps;   // diagnostics (kept, author): the decoder's structure for the first pictures
        if (dumps < 4 && (pics > 1 || dumps == 0)) { dumps++; avc_dump_detail(pics); }
        if (!direct) {   // the decoder kept them: each through Detail2's yuv[k], in display order
            for (int k = 0; k < pics && k < n_out; k++) {
                int64_t at = pts_pop(q, &qn);
                if (avc_picture(k, outs[k]) != 0) { dropped++; continue; }
                pend[np].buf = ids[k];
                pend[np++].pts = at;
            }
        } else {
            for (int k = 0; k < pics && k < n_out; k++) { pend[np].buf = ids[k]; pend[np++].pts = pts_pop(q, &qn); }
        }
        for (int k = n_out; k < pics; k++) { pts_pop(q, &qn); dropped++; }   // more than the list held (review I8)
        int dt = (int)(platform_ms() - t0);
        if (dt > worst) worst = dt;
        decoded += pics;
    }
    vth_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

// ---------- sound: MP4 / AAC ----------

static int aac_open(int rate) {
    memset(aac, 0, sizeof aac);
    int r1 = sceAudiocodecCheckNeedMem(aac, PSP_CODEC_AAC);
    int r2 = sceAudiocodecGetEDRAM(aac, PSP_CODEC_AAC);
    aac_edram = r2 >= 0;
    aac[10] = (unsigned long)rate;
    int r3 = r2 >= 0 ? sceAudiocodecInit(aac, PSP_CODEC_AAC) : -1;
    log_msg("video: aac %d Hz: needmem %08x edram %08x init %08x", rate, r1, r2, r3);
    return r3 >= 0;
}

static int mp4_audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, OUT_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) {
        log_msg("video: no audio channel %08x", ch);
        audio_on = 0;
        restart_clock(clock_ms());
        ath_alive = 0;
        sceKernelExitDeleteThread(0);
        return 0;
    }
    static int16_t outs[2][OUT_FRAMES * 2] __attribute__((aligned(64)));   // in turn (see the AVI thread)
    int cur = 0;
    int16_t *out = outs[0];
    static int16_t pcm[4096 * 2] __attribute__((aligned(64)));   // decoded stereo frames waiting
    static uint8_t in[8192] __attribute__((aligned(64)));
    static int16_t frame[2048 * 2] __attribute__((aligned(64)));
    const Mp4Track *t = &mp4.tracks[atr];
    int have = 0, my_gen = -1, ai = 0, logged = 0, fails = 0;
    PcmRs rs;
    while (!quit) {
        if (my_gen != agen) {
            my_gen = agen;
            ai = mp4_sample_at(&mp4, atr, seek_ms);
            have = 0;
            pcmrs_init(&rs, t->sample_rate, 2, PCMF_S16);
        }
        if (paused || audio_done) { sceKernelDelayThread(10000); continue; }
        int need = (int)((int64_t)OUT_FRAMES * t->sample_rate / 44100 + 2);
        while (have < need && ai < t->n_samples) {
            Mp4Sample sm;
            if (mp4_sample(&mp4, atr, ai++, &sm) != 0 || sm.size <= 0 || sm.size > (int)sizeof in) continue;
            if (fseek(af, (long)sm.offset, SEEK_SET) != 0 || fread(in, 1, (size_t)sm.size, af) != (size_t)sm.size) continue;
            sceKernelDcacheWritebackAll();
            aac[6] = (unsigned long)in;
            aac[7] = (unsigned long)sm.size;
            aac[8] = (unsigned long)frame;
            aac[9] = sizeof frame;
            int rc = sceAudiocodecDecode(aac, PSP_CODEC_AAC);
            sceKernelDcacheWritebackInvalidateAll();
            // Game mode: aac[9] = bytes out (4096, stereo 16-bit). The XMB's mode leaves it 0: an AAC frame is 1024.
            int frames = rc < 0 ? 0 : aac[9] && aac[9] != sizeof frame ? (int)(aac[9] / 4) : 1024;
            if (rc < 0 && ++fails == 50) {   // the sound never decodes: the wall clock leads (review M13)
                log_fail("video: aac decoding fails (%08x): playing without sound", rc);
                int64_t pos = clock_ms();
                audio_done = 1;
                base_ms = pos;
                wall_start = platform_ms();
            }
            if (rc >= 0) fails = 0;
            if (logged++ < 2) {
                long long e = 0;
                for (int k = 0; k < 2048; k += 3) e += (long long)frame[k] * frame[k];
                log_msg("video: aac frame rc %08x, bytes %lu, [6..12] %lx %lx %lx %lx %lx %lx %lx, energy %lld", rc, aac[9],
                        aac[6], aac[7], aac[8], aac[9], aac[10], aac[11], aac[12], e);
            }
            if (frames > (int)(sizeof pcm / 4) - have) frames = (int)(sizeof pcm / 4) - have;
            static FILE *dump;   // diagnostics (kept, author): the first 150 decoded AAC frames, once a run
            static int dumped;
            if (dumped < 150 && rc >= 0) {
                if (!dump) dump = fopen("ms0:/PSP/NEMO/logs/aac_dump.raw", "wb");
                if (dump) fwrite(frame, 4, (size_t)frames, dump);
                if (++dumped == 150 && dump) { fclose(dump); dump = NULL; log_msg("video: aac dump written (150 frames)"); }
            }
            memcpy(pcm + have * 2, frame, (size_t)frames * 4);
            have += frames;
        }
        int used = 0, n = pcmrs_run(&rs, pcm, have, &used, out, OUT_FRAMES);
        memmove(pcm, pcm + used * 2, (size_t)(have - used) * 4);
        have -= used;
        if (n < OUT_FRAMES) {
            memset(out + n * 2, 0, (size_t)(OUT_FRAMES - n) * 4);
            if (ai >= t->n_samples) {   // the sound ended: the wall clock goes on from here
                sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, out);
                if (my_gen != agen) continue;   // a seek came meanwhile (review M6)
                int64_t pos = clock_ms();
                audio_done = 1;
                base_ms = pos;
                wall_start = platform_ms();
                continue;
            }
        }
        uint32_t t_out = platform_ms();
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, out);
        if (!paused && my_gen == agen) audio_out += OUT_FRAMES;
        if (platform_ms() - t_out < 2 && audio_out > OUT_FRAMES * 4) audio_gaps++;   // returned at once: the hardware ran dry
        cur ^= 1;
        out = outs[cur];
    }
    sceAudioChRelease(ch);
    ath_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

// ---------- sound ----------

static PcmFormat pcm_format(void) {
    if (avi.a_format == 7) return PCMF_MULAW;
    return avi.a_bits == 8 ? PCMF_U8 : PCMF_S16;
}

static int frame_bytes(void) { return avi.a_channels * (avi.a_format == 7 ? 1 : avi.a_bits / 8); }

static int is_adpcm(void) { return avi.a_format == 0x11; }

// n bytes of the sound stream from chunk ci, offset co (both advanced); returns the bytes read.
static int read_sound(uint8_t *dst, int n, int *ci, int *co) {
    int got = 0;
    while (got < n && *ci < avi.n_audio) {
        int take = avi.audio[*ci].size - *co;
        if (take <= 0) { (*ci)++; *co = 0; continue; }   // review I6: a broken size never reads backwards
        if (take > n - got) take = n - got;
        if (fseek(af, (long)(avi.audio[*ci].offset + *co), SEEK_SET) != 0 || fread(dst + got, 1, (size_t)take, af) != (size_t)take) {
            *ci = avi.n_audio;
            break;
        }
        got += take;
        *co += take;
        if (*co >= avi.audio[*ci].size) { (*ci)++; *co = 0; }
    }
    return got;
}

static int audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, OUT_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) {
        log_msg("video: no audio channel %08x", ch);
        audio_on = 0;
        restart_clock(clock_ms());
        ath_alive = 0;
        sceKernelExitDeleteThread(0);
        return 0;
    }
    // Two output buffers in turn: the audio DMA still reads the one just handed over (a single buffer
    // overwritten while playing crackled, 1.3.3).
    static int16_t outs[2][OUT_FRAMES * 2] __attribute__((aligned(64)));
    int cur = 0;
    int16_t *out = outs[0];
    static uint8_t in[16384] __attribute__((aligned(4)));   // source frames (ADPCM: already decoded to 16-bit)
    static uint8_t blk[8192];
    int have = 0, my_gen = -1, ci = 0, co = 0;
    int adpcm = is_adpcm(), fpb = adpcm ? adpcm_block_frames(avi.a_block_align, avi.a_channels) : 0;
    int fb = adpcm ? avi.a_channels * 2 : frame_bytes();
    PcmRs rs;
    while (!quit) {
        if (my_gen != agen) {   // seek or resume: the chunk and offset of seek_ms in the sound
            my_gen = agen;
            long long want = adpcm ? (long long)seek_ms * avi.a_rate / 1000 / (fpb ? fpb : 1) * avi.a_block_align
                                   : avi_audio_at(&avi, seek_ms);
            if (!adpcm) want -= want % fb;
            ci = 0;
            co = 0;
            while (ci < avi.n_audio && want >= avi.audio[ci].size) { want -= avi.audio[ci].size; ci++; }
            co = (int)want;
            have = 0;
            pcmrs_init(&rs, avi.a_rate, avi.a_channels, adpcm ? PCMF_S16 : pcm_format());
        }
        if (paused || audio_done) { sceKernelDelayThread(10000); continue; }
        // fill: enough source frames for one output buffer, plus one to interpolate
        int need = (int)((int64_t)OUT_FRAMES * avi.a_rate / 44100 + 2) * fb;
        if (need > (int)sizeof in) need = (int)sizeof in - fb;
        if (adpcm) {   // whole blocks, decoded into 16-bit frames
            while (have < need && ci < avi.n_audio && have + fpb * fb <= (int)sizeof in) {
                if (read_sound(blk, avi.a_block_align, &ci, &co) != avi.a_block_align) break;
                have += adpcm_ima_block(blk, avi.a_block_align, avi.a_channels, (int16_t *)(in + have), fpb) * fb;
            }
        } else if (have < need) {
            int got = read_sound(in + have, need - have, &ci, &co);
            if (audio_be)
                for (int k = have; k + 1 < have + got; k += 2) { uint8_t t = in[k]; in[k] = in[k + 1]; in[k + 1] = t; }
            have += got;
        }
        int used = 0, n = pcmrs_run(&rs, in, have / fb, &used, out, OUT_FRAMES);
        memmove(in, in + used * fb, (size_t)(have - used * fb));
        have -= used * fb;
        if (n < OUT_FRAMES) {
            if (ci >= avi.n_audio) {   // the sound ended: the wall clock goes on from here
                memset(out + n * 2, 0, (size_t)(OUT_FRAMES - n) * 4);
                sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, out);
                if (my_gen != agen) continue;   // a seek came meanwhile (review M6)
                int64_t pos = clock_ms();
                audio_done = 1;
                base_ms = pos;
                wall_start = platform_ms();
                continue;
            }
            memset(out + n * 2, 0, (size_t)(OUT_FRAMES - n) * 4);
        }
        uint32_t t_out = platform_ms();
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, out);
        if (!paused && my_gen == agen) audio_out += OUT_FRAMES;
        if (platform_ms() - t_out < 2 && audio_out > OUT_FRAMES * 4) audio_gaps++;   // returned at once: the hardware ran dry
        cur ^= 1;
        out = outs[cur];
    }
    sceAudioChRelease(ch);
    ath_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

// ---------- UMD Video: PSMF (MPEG-PS packs of 2048 bytes) through sceMpeg's own demuxer ----------
// The way games play their movies: a ring buffer the library fills through a callback, then
// GetAvcAu/AvcDecode for the pictures (RGBA straight into our buffers) and GetAtracAu/AtracDecode for the
// ATRAC3plus sound (2048 stereo frames at 44100 Hz each). The Media Engine stays in the games' mode.
#define PS_PACK 2048
#define PS_PACKETS 512

static struct {
    SceMpeg mpeg;
    SceMpegRingbuffer rb;
    SceMpegStream *vs, *as;
    SceMpegAu *vau, *aau;
    void *mbuf, *rbuf, *es_v, *es_a;
    int created, rb_made, r_init;
    long long pos, size;   // file position in packs
    int64_t first_pts;
    volatile int eof;
    int audio_ch, audio_mask;   // the ATRAC track played, the tracks found (1.3.4)
    SceUID lock;           // the library is used by the reader, picture and sound threads
} ps;
static SceUID rth = -1;
static volatile int rth_alive;

static SceInt32 ps_fill(ScePVoid data, SceInt32 packets, ScePVoid param) {
    (void)param;
    int n = (int)fread(data, PS_PACK, (size_t)packets, vf);
    if (n <= 0) ps.eof = 1;
    return n > 0 ? n : 0;
}

static void ps_lock(void) { if (ps.lock >= 0) sceKernelWaitSema(ps.lock, 1, NULL); }
static void ps_unlock(void) { if (ps.lock >= 0) sceKernelSignalSema(ps.lock, 1); }

static int psmf_reader_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    while (!quit) {
        ps_lock();
        int avail = sceMpegRingbufferAvailableSize(&ps.rb);
        if (avail > 0 && !ps.eof) sceMpegRingbufferPut(&ps.rb, avail > 8 ? 8 : avail, avail);   // short holds of the lock (review I3)
        ps_unlock();
        sceKernelDelayThread(ps.eof ? 50000 : avail > 8 ? 1000 : 8000);   // perf review: no 1 kHz spin after the end
    }
    rth_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

static int psmf_video_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int shown = 0, errors = 0, have = 0, buf = 0, misses = 0;
    int64_t pts = 0, base = seek_ms;   // fixed for this run: a resume changes seek_ms (review C1)
    while (!quit) {
        if (have) {
            if (paused && shown) { sceKernelDelayThread(10000); continue; }
            int64_t now = clock_ms();
            if (shown && pts > now + 5) {
                int64_t wait = pts - now;
                sceKernelDelayThread((SceUInt)((wait > 20 ? 20 : wait) * 1000));
                continue;
            }
            if (shown && now - pts > LATE_MS) dropped++;
            else { ready = buf; ready_seq++; shown = 1; }
            have = 0;
            continue;
        }
        if (paused && shown) { sceKernelDelayThread(10000); continue; }
        SceInt32 unk = 0;
        ps_lock();
        int rc = sceMpegGetAvcAu(&ps.mpeg, ps.vs, ps.vau, &unk);
        ps_unlock();
        if (rc != 0) {   // no access unit yet (the reader is behind) or the end
            if (ps.eof && ++misses > 50) { ended = 1; sceKernelDelayThread(20000); continue; }
            sceKernelDelayThread(5000);
            continue;
        }
        misses = 0;
        int b = 0;
        while (b == ready || b == reading) b++;
        void *out = bufs[b];
        SceInt32 init = 0;
        uint32_t t0 = platform_ms();
        ps_lock();
        rc = sceMpegAvcDecode(&ps.mpeg, ps.vau, STRIDE_MP4, &out, &init);
        ps_unlock();
        int dt = (int)(platform_ms() - t0);
        if (dt > worst) worst = dt;
        if (rc != 0) { if (errors++ < 3) log_msg("video: umd picture rc %08x", rc); continue; }
        if (!init) continue;   // the decoder holds it back (reordering)
        sceKernelDcacheWritebackInvalidateAll();
        decoded++;
        if (ps.first_pts < 0) ps.first_pts = ps.vau->iPts;
        pts = ((int64_t)ps.vau->iPts - ps.first_pts) / 90 + base;
        buf = b;
        have = 1;
    }
    vth_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

static int psmf_audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 2048, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) {
        log_msg("video: no audio channel %08x", ch);
        audio_on = 0;
        restart_clock(clock_ms());
        ath_alive = 0;
        sceKernelExitDeleteThread(0);
        return 0;
    }
    static int16_t outs[2][2048 * 2] __attribute__((aligned(64)));   // in turn (see the AVI thread)
    int cur = 0, first = 1, errors = 0, misses = 0;
    while (!quit) {
        if (paused || audio_done) { sceKernelDelayThread(10000); continue; }
        ps_lock();
        int rc = sceMpegGetAtracAu(&ps.mpeg, ps.as, ps.aau, NULL);
        if (rc == 0) rc = sceMpegAtracDecode(&ps.mpeg, ps.aau, outs[cur], first);
        ps_unlock();
        if (rc != 0) {
            if (rc != (int)0x80618001 && errors++ < 3) log_msg("video: umd sound rc %08x", rc);
            if (ps.eof && ++misses > 50) {   // the sound ended: the wall clock goes on
                int64_t p = clock_ms();
                audio_done = 1;
                base_ms = p;
                wall_start = platform_ms();
            }
            sceKernelDelayThread(5000);
            continue;
        }
        misses = 0;
        first = 0;
        sceKernelDcacheWritebackInvalidateAll();
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, outs[cur]);
        if (!paused) audio_out += 2048;
        cur ^= 1;
    }
    sceAudioChRelease(ch);
    ath_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

static void psmf_teardown(void) {
    if (ps.created) {
        if (ps.es_v) sceMpegFreeAvcEsBuf(&ps.mpeg, ps.es_v);
        if (ps.vs) sceMpegUnRegistStream(&ps.mpeg, ps.vs);
        if (ps.as) sceMpegUnRegistStream(&ps.mpeg, ps.as);
        sceMpegDelete(&ps.mpeg);
    }
    if (ps.rb_made) sceMpegRingbufferDestruct(&ps.rb);
    if (ps.r_init == 0) sceMpegFinish();
    free(ps.mbuf);
    free(ps.rbuf);
    free(ps.es_a);
    free(ps.vau);
    free(ps.aau);
    SceUID lock = ps.lock;
    int ch = ps.audio_ch, mask = ps.audio_mask;
    memset(&ps, 0, sizeof ps);
    ps.lock = lock;
    ps.r_init = -1;
    ps.audio_ch = ch;   // the track choice outlives a rebuild
    ps.audio_mask = mask;
}

// Builds the decoder for reading from pack `at`; 0 ok.
static int psmf_build(long long at) {
    int ch = ps.audio_ch, mask = ps.audio_mask;
    psmf_teardown();
    ps.audio_ch = ch;
    ps.audio_mask = mask;
    ps.first_pts = -1;
    ps.eof = 0;
    ps.pos = at;
    if (fseek(vf, (long)(at * PS_PACK), SEEK_SET) != 0) return -1;
    ps.r_init = sceMpegInit();
    int rsize = sceMpegRingbufferQueryMemSize(PS_PACKETS);
    ps.rbuf = rsize > 0 ? memalign(64, (size_t)rsize) : NULL;
    int msize = sceMpegQueryMemSize(0);
    ps.mbuf = msize > 0 ? memalign(64, (size_t)msize) : NULL;
    ps.vau = memalign(64, 64);
    ps.aau = memalign(64, 64);
    if (!ps.rbuf || !ps.mbuf || !ps.vau || !ps.aau) return -2;
    int r1 = sceMpegRingbufferConstruct(&ps.rb, PS_PACKETS, ps.rbuf, rsize, ps_fill, NULL);
    ps.rb_made = r1 == 0;
    int r2 = sceMpegCreate(&ps.mpeg, ps.mbuf, msize, &ps.rb, STRIDE_MP4, 0, 0);
    ps.created = r2 == 0;
    if (!ps.rb_made || !ps.created) { log_msg("video: umd ring %08x create %08x", r1, r2); return -3; }
    SceMpegAvcMode mode = { -1, SCE_MPEG_AVC_FORMAT_8888 };
    sceMpegAvcDecodeMode(&ps.mpeg, &mode);
    ps.vs = sceMpegRegistStream(&ps.mpeg, 0, 0);   // video 0
    ps.as = sceMpegRegistStream(&ps.mpeg, 1, ps.audio_ch);   // ATRAC3plus, the chosen track (1.3.4)
    ps.es_v = sceMpegMallocAvcEsBuf(&ps.mpeg);
    SceInt32 es_size = 0, out_size = 0;
    sceMpegQueryAtracEsSize(&ps.mpeg, &es_size, &out_size);
    ps.es_a = es_size > 0 ? memalign(64, (size_t)es_size) : NULL;
    int r3 = sceMpegInitAu(&ps.mpeg, ps.es_v, ps.vau);
    int r4 = ps.es_a ? sceMpegInitAu(&ps.mpeg, ps.es_a, ps.aau) : -1;
    log_msg("video: umd from pack %lld: ring %d B, mpeg %d B, streams %p %p (sound track %d of mask %#x), es %p/%d, init au %08x %08x",
            at, rsize, msize, (void *)ps.vs, (void *)ps.as, ps.audio_ch, ps.audio_mask, ps.es_v, (int)es_size, r3, r4);
    if (!ps.vs || !ps.es_v || r3 != 0) return -4;
    if (r4 != 0) ps.as = NULL;
    return 0;
}

// The main film of a UMD Video disc: the largest stream (menus and trailers are small).
static int umd_main_stream(char *out, size_t cap) {
    SceUID d = sceIoDopen("disc0:/UMD_VIDEO/STREAM");
    if (d < 0) return -1;
    SceIoDirent e;
    long long best = -1;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0) {
        size_t n = strlen(e.d_name);
        if (n > 4 && !strcasecmp(e.d_name + n - 4, ".MPS") && (long long)e.d_stat.st_size > best) {
            best = (long long)e.d_stat.st_size;
            snprintf(out, cap, "disc0:/UMD_VIDEO/STREAM/%s", e.d_name);
        }
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    return best > 0 ? 0 : -1;
}

static VideoOpen open_psmf(const char *path, char *why, int cap) {
    char main_path[300];
    if (!strncmp(path, "umd:", 4)) {
        const char *file = strchr(path + 4, '/');   // a chosen stream of the disc (1.3.4)
        if (file) snprintf(main_path, sizeof main_path, "disc0:/UMD_VIDEO/STREAM/%s", file + 1);
        else if (umd_main_stream(main_path, sizeof main_path) != 0) return VO_NO_FILE;
        path = main_path;
    }
    if (vf) fclose(vf);
    vf = fopen(path, "rb");
    if (!vf) return VO_NO_FILE;
    fseek(vf, 0, SEEK_END);
    vinfo.size = ftell(vf);
    ps.size = vinfo.size / PS_PACK;
    strcpy(vinfo.container, "PSMF");
    strcpy(vinfo.vcodec, "avc1");
    strcpy(vinfo.audio, "ATRAC3plus");
    vinfo.width = W;
    vinfo.height = H;
    // the length: the first and the last presentation times
    uint8_t *b = malloc(64 * 1024);
    int64_t first = 0, last = 0;
    if (b) {
        fseek(vf, 0, SEEK_SET);
        int n = (int)fread(b, 1, 64 * 1024, vf);
        mps_first_pts(b, n, &first);
        fseek(vf, (long)(vinfo.size > 64 * 1024 ? vinfo.size - 64 * 1024 : 0), SEEK_SET);
        n = (int)fread(b, 1, 64 * 1024, vf);
        mps_last_pts(b, n, &last);
        free(b);
    }
    vinfo.duration_ms = mps_duration_ms(first, last);
    {   // the sound tracks: from the first megabyte
        uint8_t *m = malloc(1024 * 1024);
        if (m) {
            fseek(vf, 0, SEEK_SET);
            int n = (int)fread(m, 1, 1024 * 1024, vf);
            ps.audio_mask = mps_audio_tracks(m, n);
            free(m);
        }
        if (!ps.audio_mask) ps.audio_mask = 1;
        ps.audio_ch = 0;
        while (ps.audio_ch < 15 && !(ps.audio_mask & (1 << ps.audio_ch))) ps.audio_ch++;
    }
    char err[96];
    if (avc_media_load(err, sizeof err) != 0) {
        snprintf(why, (size_t)cap, "%s: %s", tr(S_VID_DECODER_FAIL), err);
        log_fail("video: umd modules: %s", err);
        return VO_UNSUPPORTED;
    }
    if (ps.lock <= 0) ps.lock = sceKernelCreateSema("nemo_psmf", 0, 1, 1, NULL);
    ps.r_init = -1;
    nbufs = NBUF;
    for (int i = 0; i < NBUF; i++) {
        bufs[i] = memalign(64, STRIDE_MP4 * H * 4);
        if (!bufs[i]) return VO_NO_MEMORY;
    }
    kind = VK_PSMF;   // set first: a failed build is undone by video_close (review M8)
    hw_frames = 2048;
    if (psmf_build(0) != 0) {
        snprintf(why, (size_t)cap, "%s", tr(S_VID_DECODER_FAIL));
        log_fail("video: umd decoder for %s", path);
        return VO_UNSUPPORTED;
    }
    audio_on = ps.as != NULL;
    log_msg("video: open %s: PSMF, %lld ms (pts %lld..%lld), %lld packs, sound %s", path, (long long)vinfo.duration_ms,
            (long long)first, (long long)last, ps.size, audio_on ? "ATRAC3plus" : "none");
    return VO_OK;
}

static void psmf_start_threads(void) {
    rth_alive = 1;
    rth = sceKernelCreateThread("nemo_umd_read", psmf_reader_thread, 0x20, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (rth < 0 || sceKernelStartThread(rth, 0, NULL) < 0) rth_alive = 0;
}

// Seeking a PSMF stream: the decoder built again at the pack that time is at (by the average bitrate).
static int psmf_seek(int64_t ms) {
    quit = 1;
    for (int i = 0; i < 1000 && (vth_alive || ath_alive || rth_alive); i++) sceKernelDelayThread(10000);
    if (vth_alive || ath_alive || rth_alive) { stuck = 1; log_fail("video: umd threads did not stop for a seek"); ended = 1; return -1; }
    long long at = vinfo.duration_ms > 0 ? ps.size * ms / vinfo.duration_ms : 0;
    if (psmf_build(at) != 0) { log_fail("video: umd seek to %lld ms failed", (long long)ms); ended = 1; return -1; }
    quit = 0;
    return 0;
}

// ---------- open / close ----------

static int fread_cb(void *ctx, long long off, void *buf, int n) {
    FILE *f = ctx;
    if (fseek(f, (long)off, SEEK_SET) != 0) return -1;
    return (int)fread(buf, 1, (size_t)n, f);
}

static int ends_with(const char *s, const char *ext) {
    size_t n = strlen(s), e = strlen(ext);
    return n > e && !strcasecmp(s + n - e, ext);
}

static int mov_as_avi(void);

static VideoOpen open_mp4(const char *path, char *why, int cap) {
    if (mp4_open(&mp4, fread_cb, vf, vinfo.size) != 0) return VO_UNSUPPORTED;
    vtr = mp4_track(&mp4, MT_VIDEO);
    atr = mp4_track(&mp4, MT_AUDIO);
    ttr = mp4_track(&mp4, MT_TEXT);
    snprintf(vinfo.container, sizeof vinfo.container, "%s", vformat_container(path));
    vinfo.duration_ms = mp4_duration_ms(&mp4);
    if (vtr < 0) return VO_UNSUPPORTED;
    const Mp4Track *t = &mp4.tracks[vtr];
    snprintf(vinfo.vcodec, sizeof vinfo.vcodec, "%s", t->codec);
    vinfo.width = t->width;
    vinfo.height = t->height;
    uint32_t delta = t->n_stts ? (uint32_t)(t->stts[4] << 24 | t->stts[5] << 16 | t->stts[6] << 8 | t->stts[7]) : 0;
    vinfo.fps_x100 = delta ? (int)((int64_t)t->timescale * 100 / delta) : 0;
    if (atr >= 0) {
        const Mp4Track *a = &mp4.tracks[atr];
        snprintf(vinfo.audio, sizeof vinfo.audio, "%s %d Hz %s", !strcmp(a->codec, "mp4a") ? "AAC" : a->codec,
                 a->sample_rate, a->channels == 1 ? "mono" : "stereo");
    }
    if (!strcmp(t->codec, "jpeg") || !strcmp(t->codec, "mjpa") || !strcmp(t->codec, "AVDJ"))
        return mov_as_avi() == 0 ? VO_MOV_MJPEG : VO_NO_MEMORY;
    if (strcmp(t->codec, "avc1") && strcmp(t->codec, "avc3")) return VO_UNSUPPORTED;
    if (t->width > W || t->height > H) {
        snprintf(why, (size_t)cap, tr(S_VID_TOO_BIG_FMT), t->width, t->height);
        return VO_UNSUPPORTED;
    }
    nal = memalign(64, NAL_MAX);
    nbufs = NBUF_MAX;
    for (int i = 0; i < NBUF_MAX; i++) {
        bufs[i] = memalign(64, STRIDE_MP4 * H * 4);
        if (!bufs[i]) return VO_NO_MEMORY;
    }
    if (!nal) return VO_NO_MEMORY;
    if (t->config_len > 1 && t->config[1] >= 100) {   // High (100) and above: not the PSP's decoder
        snprintf(why, (size_t)cap, "%s", tr(S_VID_HIGH_PROFILE));
        return VO_UNSUPPORTED;
    }
    Mp4Sample s0;   // x264 writes its settings into the first frame: B-frames in a pyramid cannot play on a PSP
    if (mp4_sample(&mp4, vtr, 0, &s0) == 0 && s0.size > 0 && s0.size <= NAL_MAX && fseek(vf, (long)s0.offset, SEEK_SET) == 0 &&
        fread(nal, 1, (size_t)s0.size, vf) == (size_t)s0.size && avc_x264_pyramid(nal, s0.size)) {
        snprintf(why, (size_t)cap, "%s", tr(S_VID_PYRAMID));
        return VO_UNSUPPORTED;
    }
    char err[96];
    if (avc_open(t->config, t->config_len, err, sizeof err) != 0) {
        snprintf(why, (size_t)cap, "%s: %s", tr(S_VID_DECODER_FAIL), err);
        log_fail("video: h264 decoder for %s: %s", path, err);
        return VO_UNSUPPORTED;
    }
    kind = VK_MP4;
    if (atr >= 0 && !strcmp(mp4.tracks[atr].codec, "mp4a")) {
        aac_ok = aac_open(mp4.tracks[atr].sample_rate);
        af = aac_ok ? fopen(path, "rb") : NULL;
    }
    audio_on = af != NULL;
    log_msg("video: open %s: %dx%d H.264 profile %d, %lld ms, sound %s", path, t->width, t->height,
            t->config_len > 1 ? t->config[1] : 0, (long long)vinfo.duration_ms, audio_on ? vinfo.audio : "none");
    return VO_OK;
}

// A camera MOV (Motion JPEG) described as an AVI: the picture and sound chunks from its sample tables, so
// the AVI threads and decoders play it unchanged.
static int mov_as_avi(void) {
    const Mp4Track *t = &mp4.tracks[vtr];
    memset(&avi, 0, sizeof avi);
    strcpy(avi.vcodec, "MJPG");
    avi.width = t->width;
    avi.height = t->height;
    avi.video = malloc((size_t)t->n_samples * sizeof *avi.video);
    if (!avi.video) return -1;
    for (int i = 0; i < t->n_samples; i++) {
        Mp4Sample sm;
        if (mp4_sample(&mp4, vtr, i, &sm) != 0) break;
        if (sm.size <= 0 || sm.size > JPEG_MAX) continue;   // review I6
        avi.video[avi.n_video].offset = sm.offset;
        avi.video[avi.n_video++].size = sm.size;
    }
    int64_t dur = vinfo.duration_ms > 0 ? vinfo.duration_ms : 1000;
    avi.us_per_frame = avi.n_video ? (uint32_t)(dur * 1000 / avi.n_video) : 33333;
    if (atr < 0) return 0;
    const Mp4Track *a = &mp4.tracks[atr];
    if (!strcmp(a->codec, "mp4a")) { mov_aac = 1; return 0; }
    int fmt = !strcmp(a->codec, "sowt") || !strcmp(a->codec, "twos") ? 1 : !strcmp(a->codec, "raw ") ? 1
            : !strcmp(a->codec, "ulaw") ? 7 : 0;
    if (!fmt) return 0;   // the pictures play without sound
    avi.has_audio = 1;
    avi.a_format = fmt;
    avi.a_channels = a->channels;
    avi.a_rate = a->sample_rate;
    avi.a_bits = !strcmp(a->codec, "raw ") ? 8 : fmt == 7 ? 8 : 16;
    audio_be = !strcmp(a->codec, "twos") && avi.a_bits == 16;
    int cap = 64;
    avi.audio = malloc((size_t)cap * sizeof *avi.audio);
    for (int i = 0; avi.audio && i < a->n_samples;) {   // one chunk per run of samples
        long long off;
        int bytes, n = mp4_run(&mp4, atr, i, 1 << 20, &off, &bytes);
        if (n <= 0) break;
        if (bytes <= 0) { i += n; continue; }   // review I6
        if (avi.n_audio == cap) {
            AviChunk *g = realloc(avi.audio, (size_t)(cap *= 2) * sizeof *g);
            if (!g) break;
            avi.audio = g;
        }
        avi.audio[avi.n_audio].offset = off;
        avi.audio[avi.n_audio++].size = bytes;
        avi.audio_bytes += bytes;
        i += n;
    }
    return 0;
}

int video_text_track(SubList *out) {
    if (kind != VK_MP4 || ttr < 0 || !vf) return 0;
    Mp4Sample sm;
    char buf[1024], text[1024];
    for (int i = 0; mp4_sample(&mp4, ttr, i, &sm) == 0; i++) {
        if (sm.size < 2 || sm.size > (int)sizeof buf) continue;
        if (fseek(vf, (long)sm.offset, SEEK_SET) != 0 || fread(buf, 1, (size_t)sm.size, vf) != (size_t)sm.size) continue;
        mp4_text(buf, sm.size, text, sizeof text);
        if (text[0]) subs_add(out, (int32_t)sm.pts_ms, (int32_t)(sm.pts_ms + sm.dur_ms), text);
    }
    subs_sort(out);
    return out->count;
}

const char *video_text_lang(void) { return kind == VK_MP4 && ttr >= 0 ? mp4.tracks[ttr].lang : ""; }

VideoOpen video_open(const char *path, VideoInfo *info, char *why, int why_cap) {
    video_close();
    paused = 0;   // the clock from zero: nothing of the last video leaks into this one (review I1)
    paused_at = base_ms = 0;
    audio_out = 0;
    audio_done = 0;
    wall_start = platform_ms();
    hw_frames = OUT_FRAMES;
    memset(&vinfo, 0, sizeof vinfo);
    why[0] = '\0';
    snprintf(vpath, sizeof vpath, "%s", path);
    if (!strncmp(path, "umd:", 4) || ends_with(path, ".mps")) {   // UMD Video (1.3.3)
        VideoOpen r = open_psmf(path, why, why_cap);
        *info = vinfo;
        if (r != VO_OK) video_close();
        else decoded = dropped = worst = audio_gaps = 0;
        return r;
    }
    struct stat st;
    vf = fopen(path, "rb");
    if (!vf || stat(path, &st) != 0) { video_close(); return VO_NO_FILE; }
    vinfo.size = (long long)st.st_size;
    if (!vformat_is_bmff(path) && !ends_with(path, ".avi")) {   // MKV, WMV, FLV...: told, not played
        snprintf(vinfo.container, sizeof vinfo.container, "%s", vformat_container(path));
        *info = vinfo;
        video_close();
        return VO_UNSUPPORTED;
    }
    int mov = vformat_is_bmff(path);
    if (mov) {
        VideoOpen r = open_mp4(path, why, why_cap);
        *info = vinfo;
        if (r == VO_MOV_MJPEG) goto mjpeg;   // a camera MOV: on as an AVI
        if (r != VO_OK) video_close();
        else decoded = dropped = worst = audio_gaps = 0;
        return r;
    }
    if (avi_open(&avi, fread_cb, vf, vinfo.size) != 0) {
        strcpy(vinfo.container, "AVI");
        *info = vinfo;
        video_close();
        return VO_UNSUPPORTED;
    }
    strcpy(vinfo.container, "AVI");
mjpeg:
    snprintf(vinfo.vcodec, sizeof vinfo.vcodec, "%s", avi.vcodec);
    vinfo.width = avi.width;
    vinfo.height = avi.height;
    vinfo.fps_x100 = avi.us_per_frame ? (int)(100000000LL / avi.us_per_frame) : 0;
    vinfo.duration_ms = avi_duration_ms(&avi);
    if (avi.has_audio && !mov)
        snprintf(vinfo.audio, sizeof vinfo.audio, "%s %d Hz %s", avi.a_format == 7 ? "mu-law" : avi.a_format == 0x11 ? "IMA ADPCM"
                 : avi.a_format == 1 ? "PCM" : avi.a_format == 0x55 ? "MP3" : "?", avi.a_rate,
                 avi.a_channels == 2 ? "stereo" : "mono");
    *info = vinfo;
    if (strcasecmp(avi.vcodec, "MJPG")) {
        video_close();
        return VO_UNSUPPORTED;
    }
    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);   // already loaded for the music: fine
    nbufs = NBUF;
    for (int i = 0; i < NBUF; i++) bufs[i] = memalign(64, W * H * 4);
    jbuf = memalign(64, JPEG_MAX);
    kind = VK_AVI;
    if (!bufs[0] || !bufs[1] || !bufs[2] || !jbuf) { video_close(); return VO_NO_MEMORY; }
    int first_ok = avi.n_video > 0 && avi.video[0].size > 0 && avi.video[0].size <= JPEG_MAX && fseek(vf, (long)avi.video[0].offset, SEEK_SET) == 0 &&
                   fread(jbuf, 1, (size_t)avi.video[0].size, vf) == (size_t)avi.video[0].size;
    int r1 = -1, r2 = -1, colour = 0, info_rc = -1;
    if (avi.width <= W && avi.height <= H) {
        r1 = sceJpegInitMJpeg();
        r2 = sceJpegCreateMJpeg(W, H);
        opened = 1;   // sceJpeg is to be finished from here on
        // The hardware takes 4:2:0 only (4:2:2 fails with 80650016): ask about the first picture.
        if (first_ok) info_rc = sceJpegGetOutputInfo(jbuf, (SceSize)avi.video[0].size, &colour, 0);
    }
    if (r1 < 0 || r2 < 0 || info_rc < 0) {   // libjpeg-turbo instead, scaled to the screen when larger
        if (opened) { sceJpegDeleteMJpeg(); sceJpegFinishMJpeg(); opened = 0; }
        soft = tjInitDecompress();
        int num = 0;
        tjscalingfactor *sf = tjGetScalingFactors(&num);
        soft_w = soft_h = 0;
        // Full size ran at 14-16 frames/s on the PSP (1.3.3, console): decoded at most 256x192 instead
        // (480x272 -> 240x136, 640x480 -> 240x180) and stretched to the screen — smooth, a little softer.
        for (int i = 0; sf && i < num; i++) {
            int w2 = TJSCALED(avi.width, sf[i]), h2 = TJSCALED(avi.height, sf[i]);
            if (w2 <= SOFT_W && h2 <= SOFT_H && w2 * h2 > soft_w * soft_h) { soft_w = w2; soft_h = h2; }
        }
        if (!soft || !soft_w || !first_ok ||
            tjDecompress2(soft, jbuf, (unsigned long)avi.video[0].size, (unsigned char *)bufs[0], soft_w, soft_w * 4, soft_h,
                          TJPF_RGBX, TJFLAG_FASTDCT) != 0) {
            log_msg("video: %s: neither decoder takes the picture (sceJpeg %08x/%08x/%08x, turbo %s)", path, r1, r2, info_rc,
                    soft ? tjGetErrorStr2(soft) : "-");
            snprintf(why, (size_t)why_cap, "%s", tr(S_VID_DECODER_FAIL));
            log_fail("video: no decoder takes %s", path);
            video_close();
            return VO_UNSUPPORTED;
        }
        vinfo.width = soft_w;
        vinfo.height = soft_h;
    }
    log_msg("video: open %s: %dx%d %s, %lld ms, sound %s; %s (sceJpeg %08x %08x info %08x)", path, avi.width, avi.height,
            avi.vcodec, (long long)vinfo.duration_ms, vinfo.audio[0] ? vinfo.audio : "none",
            soft ? "software decoding" : "hardware decoding", r1, r2, info_rc);
    if (soft) log_msg("video: software pictures %dx%d", soft_w, soft_h);
    if (mov_aac) {   // the sound of a camera MOV in AAC: the MP4 sound thread
        aac_ok = aac_open(mp4.tracks[atr].sample_rate);
        af = aac_ok ? fopen(path, "rb") : NULL;
        audio_on = af != NULL;
        decoded = dropped = worst = audio_gaps = 0;
        *info = vinfo;
        return VO_OK;
    }
    af = avi.has_audio ? fopen(path, "rb") : NULL;
    int fpb = adpcm_block_frames(avi.a_block_align, avi.a_channels);
    audio_on = af != NULL && avi.a_rate > 0 && avi.a_rate <= 96000 && avi.a_channels > 0 && avi.a_channels <= 2 &&
               ((avi.a_format == 1 && (avi.a_bits == 8 || avi.a_bits == 16)) || avi.a_format == 7 ||   // review M2
                (avi.a_format == 0x11 && fpb > 0 && avi.a_block_align <= 8192 && fpb * avi.a_channels * 2 <= 8192));   // review M10
    if (avi.has_audio && !audio_on) log_msg("video: sound format %#x not played", avi.a_format);
    decoded = dropped = worst = audio_gaps = 0;
    *info = vinfo;   // the picture size may have become the scaled one
    return VO_OK;
}

void video_play(int64_t from_ms) {
    if (kind == VK_NONE) return;
    quit = 0;
    paused = 0;
    ended = 0;
    ready = -1;
    seek_ms = from_ms;
    gen++;
    agen++;
    restart_clock(from_ms);
    vth_alive = 1;
    if (kind == VK_PSMF) psmf_start_threads();
    vth = sceKernelCreateThread("nemo_video", kind == VK_PSMF ? psmf_video_thread : kind == VK_MP4 ? mp4_video_thread : video_thread,
                                0x30, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (vth < 0 || sceKernelStartThread(vth, 0, NULL) < 0) vth_alive = 0;
    if (audio_on) {
        ath_alive = 1;
        ath = sceKernelCreateThread("nemo_video_snd", kind == VK_PSMF ? psmf_audio_thread : kind == VK_MP4 || mov_aac ? mp4_audio_thread : audio_thread, 0x12, 0x8000,
                                    PSP_THREAD_ATTR_USER, NULL);
        if (ath < 0 || sceKernelStartThread(ath, 0, NULL) < 0) { ath_alive = 0; audio_on = 0; }
    }
}

void video_pause(int on) {
    if (on == paused) return;
    if (on) {
        paused_at = clock_ms();
        paused = 1;
    } else {
        seek_ms = paused_at;
        agen++;           // the sound starts over exactly here; the pictures go on (review I9)
        restart_clock(paused_at);
        paused = 0;
    }
}

int video_paused(void) { return paused; }

void video_seek(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms > vinfo.duration_ms - 500) ms = vinfo.duration_ms > 500 ? vinfo.duration_ms - 500 : 0;
    if (kind == VK_PSMF) {   // rebuilt at the new place, then played from there
        int was_paused = paused;
        if (psmf_seek(ms) != 0) return;   // the decoder is gone: no threads on it (review I2)
        video_play(ms);
        if (was_paused) video_pause(1);
        return;
    }
    ended = 0;
    seek_ms = ms;
    gen++;   // first, then the clock (review M7)
    agen++;
    if (paused) paused_at = ms;
    else restart_clock(ms);
}

int video_ended(void) { return ended; }

int video_audio_tracks(int *current) {
    if (current) *current = ps.audio_ch;
    return kind == VK_PSMF ? ps.audio_mask : 0;
}

void video_set_audio_track(int ch) {
    if (kind != VK_PSMF || ch < 0 || ch > 15 || !(ps.audio_mask & (1 << ch)) || ch == ps.audio_ch) return;
    log_msg("video: umd sound track %d -> %d", ps.audio_ch, ch);
    ps.audio_ch = ch;
    video_seek(video_pos_ms());   // rebuilt at this place with the other track
}

int video_umd_streams(VideoStream *out, int max) {
    SceUID d = sceIoDopen("disc0:/UMD_VIDEO/STREAM");
    if (d < 0) return 0;
    SceIoDirent e;
    int n = 0;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0 && n < max) {
        size_t l = strlen(e.d_name);
        if (l > 4 && l < sizeof out[0].file && !strcasecmp(e.d_name + l - 4, ".MPS")) {
            snprintf(out[n].file, sizeof out[n].file, "%s", e.d_name);
            out[n].size = (long long)e.d_stat.st_size;
            out[n].duration_ms = -1;
            n++;
        }
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    for (int i = 1; i < n; i++)   // by name: the disc's own order
        for (int j = i; j > 0 && strcasecmp(out[j - 1].file, out[j].file) > 0; j--) {
            VideoStream t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    uint32_t t0 = platform_ms();
    uint8_t *b = malloc(64 * 1024);
    for (int i = 0; b && i < n; i++) {   // lengths from the first and last presentation times
        char p[300];
        snprintf(p, sizeof p, "disc0:/UMD_VIDEO/STREAM/%s", out[i].file);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        int64_t first = 0, last = 0;
        int k = (int)fread(b, 1, 64 * 1024, f);
        mps_first_pts(b, k, &first);
        fseek(f, (long)(out[i].size > 64 * 1024 ? out[i].size - 64 * 1024 : 0), SEEK_SET);
        k = (int)fread(b, 1, 64 * 1024, f);
        mps_last_pts(b, k, &last);
        fclose(f);
        out[i].duration_ms = mps_duration_ms(first, last);
        log_dbg("video: umd stream %s: %lld bytes, %lld ms", out[i].file, out[i].size, (long long)out[i].duration_ms);
    }
    free(b);
    log_msg("video: umd streams %d listed in %u ms", n, (unsigned)(platform_ms() - t0));
    return n;
}

int video_take_frame(const uint32_t **px, int *w, int *h, int *stride) {
    if (ready < 0 || ready_seq == taken_seq) return 0;
    reading = ready;
    taken_seq = ready_seq;
    *px = bufs[reading];
    *w = vinfo.width;
    *h = vinfo.height;
    *stride = kind == VK_MP4 || kind == VK_PSMF ? STRIDE_MP4 : vinfo.width;   // Motion JPEG comes out packed (probe 5); H.264 512 a row
    return 1;
}

void video_frame_done(void) { reading = -1; }

int video_sound_late(void) { return audio_gaps; }

void video_stats(int *d, int *dr, int *wo) {
    *d = decoded;
    *dr = dropped;
    *wo = worst;
}

void video_close(void) {
    quit = 1;
    for (int i = 0; i < 1000 && (vth_alive || ath_alive || rth_alive); i++) sceKernelDelayThread(10000);
    if (vth_alive || ath_alive || rth_alive) {   // a thread hangs in a decoder or a read: never free under it (review I3)
        stuck = 1;
        log_fail("video: threads did not stop (picture %d sound %d reader %d): their memory is left", vth_alive, ath_alive, rth_alive);
    }
    if (stuck) {   // forget everything, free nothing: a leak instead of a crash under a live thread
        for (int i = 0; i < NBUF_MAX; i++) bufs[i] = NULL;
        jbuf = NULL;
        nal = NULL;
        vf = af = NULL;
        soft = NULL;
        memset(&ps, 0, sizeof ps);
        ps.lock = -1;
        ps.r_init = -1;
        memset(&mp4, 0, sizeof mp4);
        memset(&avi, 0, sizeof avi);
        opened = aac_edram = aac_ok = audio_be = mov_aac = 0;
        vtr = atr = ttr = -1;
        kind = VK_NONE;
        ready = reading = -1;
        stuck = 0;
        vth_alive = ath_alive = rth_alive = 0;
        quit = 0;
        return;
    }
    if (kind == VK_PSMF) psmf_teardown();
    if (opened) {
        sceJpegDeleteMJpeg();
        sceJpegFinishMJpeg();
        opened = 0;
    }
    if (soft) tjDestroy(soft);
    soft = NULL;
    audio_be = mov_aac = 0;
    if (aac_edram) sceAudiocodecReleaseEDRAM(aac);
    aac_edram = aac_ok = 0;
    if (kind == VK_MP4 || vtr >= 0) avc_close();
    mp4_close(&mp4);
    vtr = atr = ttr = -1;
    free(nal);
    nal = NULL;
    kind = VK_NONE;
    if (vf) fclose(vf);
    if (af) fclose(af);
    vf = af = NULL;
    for (int i = 0; i < NBUF_MAX; i++) { free(bufs[i]); bufs[i] = NULL; }
    nbufs = 0;
    free(jbuf);
    jbuf = NULL;
    avi_close(&avi);
    ready = reading = -1;
    vth = ath = -1;
    quit = 0;
}
