// H.264 in MP4 (1.3.3): the way the XMB's own player decodes MP4. The Media Engine is
// booted in the XMB's mode 3 (games get 2, which is why sceMpegAvcDecode refused every frame before:
// 80628002) through sceMeCore_driver's sceMeBootStart (6.60/6.61 NID 0x5DFF5C50) called in kernel mode;
// the sceMpeg of mpeg_vsh.prx takes the MP4 samples as they are (sceMpegGetAvcNalAu). On closing the
// Media Engine goes back to mode 2 for the music. Console probes 10-11: Baseline and Main decode at ~5 ms a
// frame; B-frames in a pyramid (x264's default) are refused by the PSP itself (the XMB refuses them too).
// The call order is cooleyes' avcdecoder sample (2009, GPL-2.0-or-later); the boot mode is PPSSPP's finding
// (PR #22768); the NID LightMP3's.
#include "avc_hw.h"
#include "log.h"
#include "mp4.h"
#include "platform.h"
#include <kubridge.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pspkernel.h>
#include <pspmpeg.h>
#include <pspsuspend.h>
#include <psputility.h>
#include <systemctrl.h>

typedef struct {
    ScePVoid sps_buffer;
    SceInt32 sps_size;
    ScePVoid pps_buffer;
    SceInt32 pps_size;
    SceInt32 nal_prefix_size;
    ScePVoid nal_buffer;
    SceInt32 nal_size;
    SceInt32 mode;          // 3 for the first frame, then 0
} AvcNal;

typedef struct { SceInt32 unknown0, unknown1, width, height, unknown4[6]; } AvcInfo;
typedef struct { ScePVoid buffer[8]; SceInt32 unknown[3]; } AvcYuv;
typedef struct {
    SceInt32 unknown0[4];
    AvcInfo *info;
    SceInt32 unknown5[6];
    AvcYuv *yuv;
    SceInt32 unknown12[12];
} AvcDetail2;
typedef struct {
    SceInt32 height, width, mode0, mode1;   // in 16-pixel blocks
    ScePVoid buffer[8];
} AvcCsc;

SceInt32 sceMpegGetAvcNalAu(SceMpeg *mpeg, AvcNal *nal, SceMpegAu *au);
SceInt32 sceMpegAvcDecodeDetail2(SceMpeg *mpeg, AvcDetail2 **detail);
SceInt32 sceMpegBaseCscAvc(void *rgb, unsigned int unknown, unsigned int width, AvcCsc *csc);

#define STRIDE 512
#define NID_ME_BOOT_660 0x5DFF5C50

static int modules_loaded;
static void *me_boot;

static int me_boot_start(int mode) {
    if (!me_boot) me_boot = (void *)sctrlHENFindFunction("sceMeCodecWrapper", "sceMeCore_driver", NID_ME_BOOT_660);
    if (!me_boot) me_boot = (void *)sctrlHENFindFunction(NULL, "sceMeCore_driver", NID_ME_BOOT_660);
    if (!me_boot) return -1;
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    args.arg1 = (u32)mode;
    int rc = kuKernelCall(me_boot, &args);
    return rc < 0 ? rc : (int)args.ret1;
}

static int load_start(const char *path) {
    SceUID id = kuKernelLoadModule(path, 0, NULL);
    if (id < 0) return id;
    int status = 0;
    int rc = sceKernelStartModule(id, 0, NULL, &status, NULL);
    return rc < 0 ? rc : id;
}

// avcodec (utility), then the XMB's mpegbase and mpeg from flash0 (they come with the firmware).
static int load_modules(char *why, int cap) {
    int a = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);   // every time: turning the music off unloads it (review M12)
    if (modules_loaded) return 0;
    int b = load_start("flash0:/kd/mpegbase_260.prx");
    int c = load_start("flash0:/kd/mpeg_vsh.prx");
    log_msg("avc: avcodec %08x, mpegbase_260 %08x, mpeg_vsh %08x", a, b, c);
    // 8002013B: the library is already there (avcodec brings sceMpegbase; mpeg_vsh stays loaded between videos).
    if (c < 0 && c != (int)0x8002013B) {
        snprintf(why, (size_t)cap, "mpeg_vsh %08x", c);
        return -1;
    }
    modules_loaded = 1;
    return 0;
}

// avcC -> SPS and PPS without their lengths, and the NAL length size.
static int avcc_params(const uint8_t *c, int n, uint8_t *sps, int *sps_n, uint8_t *pps, int *pps_n, int *prefix) {
    if (n < 8) return -1;
    *prefix = (c[4] & 3) + 1;
    int p = 5;
    if ((c[p] & 0x1f) < 1) return -1;
    int len = c[p + 1] << 8 | c[p + 2];
    p += 3;
    if (p + len > n || len > 256) return -1;
    memcpy(sps, c + p, (size_t)len);
    *sps_n = len;
    p += len;
    for (int i = 0, k = (c[5] & 0x1f) - 1; i < k; i++) {   // extra SPS (review M1: bounds)
        if (p + 2 > n) return -1;
        int l = c[p] << 8 | c[p + 1];
        p += 2 + l;
    }
    if (p + 3 > n || c[p] < 1) return -1;
    len = c[p + 1] << 8 | c[p + 2];
    p += 3;
    if (p + len > n || len > 256) return -1;
    memcpy(pps, c + p, (size_t)len);
    *pps_n = len;
    return 0;
}

// ---------- the decoding session (one at a time) ----------

static struct {
    int open, first, prefix, sps_n, pps_n, r_init, vol_locked;
    SceMpeg mpeg;
    SceMpegRingbuffer rb;
    SceMpegAu *au;
    void *mbuf, *vol;
    uint8_t *spspps;
} av;

int avc_open(const uint8_t *avcc, int avcc_len, char *why, int cap) {
    avc_close();
    uint8_t sps[256], pps[256];
    if (avcc_params(avcc, avcc_len, sps, &av.sps_n, pps, &av.pps_n, &av.prefix) != 0) { snprintf(why, (size_t)cap, "avcC"); return -1; }
    if (load_modules(why, cap) != 0) return -1;
    int boot = me_boot_start(3);   // the XMB's mode (PPSSPP: every type but 2 boots as 3 from the PSP-2000 on)
    av.r_init = sceMpegInit();
    int msize = sceMpegQueryMemSize(4);
    av.mbuf = msize > 0 ? memalign(64, (size_t)((msize + 15) & ~15)) : NULL;
    int vsize = 0, r_vol = sceKernelVolatileMemTryLock(0, &av.vol, &vsize);   // 4 MB at 0x08400000: the ddrtop
    av.vol_locked = r_vol >= 0;
    av.au = memalign(64, 64);
    av.spspps = memalign(64, 512);
    int r_cr = -1;
    if (av.mbuf && av.vol_locked && av.au && av.spspps) {
        memset(&av.rb, 0, sizeof av.rb);
        r_cr = sceMpegCreate(&av.mpeg, av.mbuf, msize, &av.rb, STRIDE, 4, (SceInt32)av.vol);
    }
    log_msg("avc: open: me %08x, init %08x, mem %d, volatile %08x, create %08x", boot, av.r_init, msize, r_vol, r_cr);
    if (r_cr != 0) {
        snprintf(why, (size_t)cap, "sceMpegCreate %08x / volatile %08x", r_cr, r_vol);
        av.open = 1;   // so that close undoes what was done
        avc_close();
        return -1;
    }
    memset(av.au, 0xFF, 64);
    sceMpegInitAu(&av.mpeg, (uint8_t *)av.vol + 0x10000, av.au);
    SceMpegAvcMode mode = { -1, SCE_MPEG_AVC_FORMAT_8888 };   // for pictures written straight into our buffers
    log_msg("avc: decode mode %08x", (unsigned)sceMpegAvcDecodeMode(&av.mpeg, &mode));
    memcpy(av.spspps, sps, (size_t)av.sps_n);
    memcpy(av.spspps + av.sps_n, pps, (size_t)av.pps_n);
    av.open = 2;
    av.first = 1;
    return 0;
}

void avc_restart(void) { av.first = 1; }

#define MARK 0xDEADBEEFu

int avc_feed(uint8_t *nal, int size, uint32_t **out, int n_out, int *pictures, int *direct) {
    *pictures = 0;
    *direct = 0;
    if (av.open != 2) return -1;
    AvcNal n = { av.spspps, av.sps_n, av.spspps + av.sps_n, av.pps_n, av.prefix, nal, size, av.first ? 3 : 0 };
    sceKernelDcacheWritebackAll();
    int r = sceMpegGetAvcNalAu(&av.mpeg, &n, av.au);
    if (r != 0) return r;
    // A list of buffers (PPSSPP: games pass one, and the decoder may fill more than one): marked first, so a
    // written one is told from an untouched one.
    for (int k = 0; k < n_out; k++) out[k][STRIDE * 136 + 240] = MARK;
    sceKernelDcacheWritebackAll();
    SceInt32 pics = 0;
    r = sceMpegAvcDecode(&av.mpeg, av.au, STRIDE, n_out ? (void *)out : 0, &pics);
    if (r != 0) return r;
    av.first = 0;
    *pictures = (int)pics;
    sceKernelDcacheWritebackInvalidateAll();
    int written = 0;
    for (int k = 0; k < n_out && k < (int)pics; k++) written += out[k][STRIDE * 136 + 240] != MARK;
    if (pics > 0 && written == (int)pics) *direct = 1;
    return 0;
}

// Detail2's yuv is an array, one entry per picture of the call (console log, 1.3.3): with B-frames a call
// may give two, the B-frame just decoded (index 0) and the reference frame shown after it (index 1).
int avc_picture(int index, uint32_t *rgb) {
    if (av.open != 2 || index < 0 || index > 3) return -1;
    AvcDetail2 *d = NULL;
    int r = sceMpegAvcDecodeDetail2(&av.mpeg, &d);
    if (r != 0 || !d || !d->info || !d->yuv) return r ? r : -1;
    AvcCsc csc;
    csc.height = (d->info->height + 15) >> 4;
    csc.width = (d->info->width + 15) >> 4;
    csc.mode0 = csc.mode1 = 0;
    for (int k = 0; k < 8; k++) csc.buffer[k] = d->yuv[index].buffer[k];
    r = sceMpegBaseCscAvc(rgb, 0, STRIDE, &csc);
    sceKernelDcacheWritebackInvalidateAll();
    return r;
}

// Diagnostics (kept on purpose, author 2026-10-30): the Detail2 structure and its YUV block, raw.
void avc_dump_detail(int pics) {
    AvcDetail2 *d = NULL;
    int r = sceMpegAvcDecodeDetail2(&av.mpeg, &d);
    if (r != 0 || !d) { log_msg("avc: detail2 %08x", r); return; }
    const uint32_t *w = (const uint32_t *)d;
    char line[400];
    int n = snprintf(line, sizeof line, "avc: pics %d detail2 @%p:", pics, (void *)d);
    for (int k = 0; k < 24 && n < (int)sizeof line - 10; k++) n += snprintf(line + n, sizeof line - (size_t)n, " %lx", (unsigned long)w[k]);
    log_msg("%s", line);
    if (d->info) {
        const uint32_t *iw = (const uint32_t *)d->info;
        log_msg("avc: info %lx %lx %lx %lx %lx %lx %lx %lx", (unsigned long)iw[0], (unsigned long)iw[1], (unsigned long)iw[2],
                (unsigned long)iw[3], (unsigned long)iw[4], (unsigned long)iw[5], (unsigned long)iw[6], (unsigned long)iw[7]);
    }
    if (d->yuv) {
        const uint32_t *yw = (const uint32_t *)d->yuv;
        n = snprintf(line, sizeof line, "avc: yuv @%p:", (void *)d->yuv);
        for (int k = 0; k < 24 && n < (int)sizeof line - 10; k++) n += snprintf(line + n, sizeof line - (size_t)n, " %lx", (unsigned long)yw[k]);
        log_msg("%s", line);
    }
}

void avc_close(void) {
    if (!av.open) return;
    if (av.open == 2) sceMpegDelete(&av.mpeg);
    if (av.r_init == 0) sceMpegFinish();
    if (av.vol_locked) sceKernelVolatileMemUnlock(0);
    free(av.spspps);
    free(av.au);
    free(av.mbuf);
    int r = me_boot_start(2);   // the games' mode again: the music's MP3 decoder runs there
    log_msg("avc: closed, me back to games' mode %08x", r);
    memset(&av, 0, sizeof av);
}

int avc_media_load(char *why, int cap) { return load_modules(why, cap); }

// x264 writes its settings into the first frame (an SEI): "b_pyramid=2" means B-frames in a pyramid.
int avc_x264_pyramid(const uint8_t *sample, int size) {
    static const char KEY[] = "b_pyramid=";
    for (int i = 0; i + (int)sizeof KEY < size; i++)
        if (sample[i] == 'b' && !memcmp(sample + i, KEY, sizeof KEY - 1)) return sample[i + sizeof KEY - 1] != '0';
    return 0;
}
