// Go!Cam (PSP-450) through sceUsbCam (1.3.6). The way the pspsdk camera sample does it: the USB accessory and
// camera modules, the USB bus/accessory/camera/microphone drivers, activation with the camera's PID, then
// sceUsbCamSetupVideo + StartVideo and ReadVideoFrameBlocking for Motion JPEG frames, decoded by sceJpeg
// (libjpeg-turbo when it refuses). Stills: the video stopped, SetupStill 640x480, StillInputBlocking.
// No camera was at hand when this was written: every return code goes to the log.
#include "camera_hw.h"
#include "aviwrite.h"
#include "log.h"
#include "platform.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kubridge.h>
#include <pspjpeg.h>
#include <pspkernel.h>
#include <pspusb.h>
#include <pspusbacc.h>
#include <pspusbcam.h>
#include <psputility.h>
#include <turbojpeg.h>

// In libpspusbcam but missing from pspusbcam.h.
int sceUsbCamStartMic(void);
int sceUsbCamStopMic(void);
int sceUsbCamReadMicBlocking(void *buf, SceSize size);

#define W 480
#define H 272
#define JPEG_MAX (256 * 1024)
#define STILL_MAX (512 * 1024)
#define STILL_W 640
#define STILL_H 480
#define REC_FPS 15
#define MIC_RATE 22050
#define MIC_CHUNK 2048          // bytes per microphone read (~46 ms)
#define CONNECT_MS 4000
#define AVI_LIMIT 1900000000L   // AVI 1.0 and FAT32: stop well before 2 GB

static volatile CamState state = CS_OFF;
static SceUID conn_th = -1, cam_th = -1, mic_th = -1;
static volatile int quit, cam_alive, mic_alive;
static int mods_loaded, drivers_started, activated, jpeg_open, video_on, mic_on;
static uint8_t *jbuf, *still;
static uint32_t *bufs[3];
static volatile int ready = -1, reading = -1, ready_seq, taken_seq;
static volatile CamFilter filter = CF_NONE;
static volatile int zoom = 10, lens;
static volatile float fps;
static tjhandle soft;
static u8 work[68 * 1024] __attribute__((aligned(64)));
static u8 mic_work[16 * 1024] __attribute__((aligned(64)));
// photo request / result
static volatile int photo_req, photo_done, photo_rc;
static char photo_path[200];
// recording
static AviWriter avi;
static SceUID avi_lock = -1;
static volatile int rec_on, rec_stopped_self;
static uint32_t rec_t0, rec_slots;
static volatile uint32_t rec_ms;

static void lock(void) { if (avi_lock >= 0) sceKernelWaitSema(avi_lock, 1, NULL); }
static void unlock(void) { if (avi_lock >= 0) sceKernelSignalSema(avi_lock, 1); }

// Loaded straight from flash0 like usb.c does: sceUtilityLoadUsbModule's stub sits apart from the other
// sceUtility stubs and breaks the import table (psp-fixup-imports "stubs out of order").
static SceUID mod_ids[2] = { -1, -1 };

static int load_modules(void) {
    static const char *const mods[] = { "flash0:/kd/usbacc.prx", "flash0:/kd/usbcam.prx" };
    for (unsigned i = 0; i < 2; i++) {
        SceUID id = kuKernelLoadModule(mods[i], 0, NULL);
        int rc = id >= 0 ? sceKernelStartModule(id, 0, NULL, NULL, NULL) : id;
        mod_ids[i] = id >= 0 && rc >= 0 ? id : -1;   // 80020139/8002013C: already there (a game or the XMB)
        log_msg("camera: %s load=%08x start=%08x", mods[i], (unsigned)id, (unsigned)rc);
    }
    mods_loaded = 1;
    return 0;
}

static void start_drivers(void) {
    int r1 = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    int r2 = sceUsbStart(PSP_USBACC_DRIVERNAME, 0, 0);
    int r3 = sceUsbStart(PSP_USBCAM_DRIVERNAME, 0, 0);
    int r4 = sceUsbStart(PSP_USBCAMMIC_DRIVERNAME, 0, 0);
    drivers_started = 1;
    int r5 = sceUsbActivate(PSP_USBCAM_PID);
    activated = r5 >= 0;
    log_msg("camera: drivers bus %08x acc %08x cam %08x mic %08x, activate %08x, state %08x", (unsigned)r1, (unsigned)r2,
            (unsigned)r3, (unsigned)r4, (unsigned)r5, (unsigned)sceUsbGetState());
}

static void stop_drivers(void) {
    if (activated) sceUsbDeactivate(PSP_USBCAM_PID);
    activated = 0;
    if (drivers_started) {
        sceUsbStop(PSP_USBCAMMIC_DRIVERNAME, 0, 0);
        sceUsbStop(PSP_USBCAM_DRIVERNAME, 0, 0);
        sceUsbStop(PSP_USBACC_DRIVERNAME, 0, 0);
        sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    }
    drivers_started = 0;
    for (int i = 1; mods_loaded && i >= 0; i--) {   // camera first, then the accessory base
        if (mod_ids[i] < 0) continue;
        int st = sceKernelStopModule(mod_ids[i], 0, NULL, NULL, NULL);
        int un = sceKernelUnloadModule(mod_ids[i]);
        log_dbg("camera: module %d stop %08x unload %08x", i, (unsigned)st, (unsigned)un);
        mod_ids[i] = -1;
    }
    mods_loaded = 0;
}

static int setup_video(void) {
    PspUsbCamSetupVideoParam p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.resolution = PSP_USBCAM_RESOLUTION_480_272;
    p.framerate = PSP_USBCAM_FRAMERATE_30_FPS;
    p.wb = PSP_USBCAM_WB_AUTO;
    p.saturation = 125;
    p.brightness = 128;
    p.contrast = 64;
    p.sharpness = 0;
    p.effectmode = PSP_USBCAM_EFFECTMODE_NORMAL;
    p.framesize = JPEG_MAX;
    p.evlevel = PSP_USBCAM_EVLEVEL_0_0;
    int r1 = sceUsbCamSetupVideo(&p, work, sizeof work);
    int r2 = sceUsbCamAutoImageReverseSW(1);
    int r3 = sceUsbCamStartVideo();
    int r4 = sceUsbCamSetZoom(zoom);
    video_on = r3 >= 0;
    log_msg("camera: video setup %08x reverse %08x start %08x zoom %08x", (unsigned)r1, (unsigned)r2, (unsigned)r3, (unsigned)r4);
    return r1 >= 0 && r3 >= 0 ? 0 : -1;
}

static void setup_mic(void) {
    PspUsbCamSetupMicParam m;
    memset(&m, 0, sizeof m);
    m.size = sizeof m;
    m.alc = 1;          // automatic level
    m.gain = 0;
    m.noize = 1;
    m.freq = MIC_RATE;
    int r1 = sceUsbCamSetupMic(&m, mic_work, sizeof mic_work);
    int r2 = r1 >= 0 ? sceUsbCamStartMic() : -1;
    mic_on = r2 >= 0;
    log_msg("camera: mic setup %08x start %08x", (unsigned)r1, (unsigned)r2);
}

static int decode(const uint8_t *jpg, int size, uint32_t *out) {
    if (jpeg_open) {
        int rc = sceJpegDecodeMJpeg((u8 *)jpg, (SceSize)size, (u8 *)out, 0);
        if (rc >= 0) return 0;
        static int logged;
        if (logged++ < 3) log_msg("camera: sceJpeg %08x on a %d-byte frame: libjpeg-turbo from now on", (unsigned)rc, size);
        sceJpegDeleteMJpeg();
        sceJpegFinishMJpeg();
        jpeg_open = 0;
    }
    if (!soft) soft = tjInitDecompress();
    if (!soft) return -1;
    return tjDecompress2(soft, jpg, (unsigned long)size, (unsigned char *)out, W, W * 4, H, TJPF_RGBX, TJFLAG_FASTDCT);
}

// photo: the still, filtered, as a JPEG file (unfiltered = the camera's own bytes)
static int save_jpeg(const char *path, const uint8_t *data, unsigned long size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0) ok = 0;
    if (!ok) remove(path);
    return ok ? 0 : -1;
}

static int photo_from(const uint8_t *jpg, int size, int w, int h) {
    CamFilter f = filter;
    if (f == CF_NONE) return save_jpeg(photo_path, jpg, (unsigned long)size);
    uint8_t *rgba = memalign(64, (size_t)w * h * 4);
    tjhandle d = tjInitDecompress(), c = tjInitCompress();
    unsigned char *out = NULL;
    unsigned long out_size = 0;
    int rc = -1;
    if (rgba && d && c && tjDecompress2(d, jpg, (unsigned long)size, rgba, w, w * 4, h, TJPF_RGBX, 0) == 0) {
        camfilter_apply(rgba, w, h, w, f, platform_ms());
        if (tjCompress2(c, rgba, w, w * 4, h, TJPF_RGBX, &out, &out_size, TJSAMP_420, 92, 0) == 0)
            rc = save_jpeg(photo_path, out, out_size);
        else log_msg("camera: jpeg encode: %s", tjGetErrorStr2(c));
    } else log_msg("camera: still decode failed (%dx%d, %d bytes)", w, h, size);
    if (out) tjFree(out);
    if (d) tjDestroy(d);
    if (c) tjDestroy(c);
    free(rgba);
    return rc;
}

static void take_photo(const uint8_t *last, int last_size) {
    uint32_t t0 = platform_ms();
    int r0 = sceUsbCamStopVideo();
    video_on = 0;
    PspUsbCamSetupStillParam p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.resolution = PSP_USBCAM_RESOLUTION_640_480;
    p.jpegsize = STILL_MAX;
    p.reverseflags = 0;
    p.delay = PSP_USBCAM_NODELAY;
    p.complevel = 8;
    int r1 = sceUsbCamSetupStill(&p);
    int n = r1 >= 0 ? sceUsbCamStillInputBlocking(still, STILL_MAX) : -1;
    log_msg("camera: still stop %08x setup %08x input %d (%u ms)", (unsigned)r0, (unsigned)r1, n, (unsigned)(platform_ms() - t0));
    int rc;
    if (n > 0) rc = photo_from(still, n, STILL_W, STILL_H);
    else if (last_size > 0) {   // no still: the viewfinder's frame instead (480x272)
        log_msg("camera: still failed, the viewfinder frame saved instead");
        rc = photo_from(last, last_size, W, H);
    } else rc = -1;
    setup_video();
    log_msg("camera: photo %s rc %d in %u ms (filter %d)", photo_path, rc, (unsigned)(platform_ms() - t0), (int)filter);
    photo_rc = rc;
    photo_done = 1;
}

static void record_frame(const uint8_t *jpg, int size) {
    lock();
    if (rec_on) {
        uint32_t due = (platform_ms() - rec_t0) * REC_FPS / 1000 + 1;   // constant 15 frames/s
        int rc = 0;
        if (rec_slots < due) { rc = aviw_video(&avi, jpg, (uint32_t)size); rec_slots++; }
        while (rc == 0 && rec_slots < due) { rc = aviw_video(&avi, NULL, 0); rec_slots++; }   // late: repeat
        rec_ms = (platform_ms() - rec_t0);
        if (rc != 0 || aviw_size(&avi) > AVI_LIMIT) {
            log_fail("camera: recording stopped (%s), %u frames", rc ? "write failed" : "2 GB", (unsigned)avi.n_video);
            rec_on = 0;
            int c = aviw_close(&avi);
            log_msg("camera: file closed rc %d", c);
            rec_stopped_self = 1;
        }
    }
    unlock();
}

static int cam_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int frames = 0, errors = 0, last_size = 0;
    uint32_t t_fps = platform_ms(), t_lens = 0;
    static uint8_t last[JPEG_MAX] __attribute__((aligned(64)));
    while (!quit) {
        if (photo_req) {
            photo_req = 0;
            take_photo(last, last_size);
            continue;
        }
        if (!video_on) { sceKernelDelayThread(100000); if (setup_video() != 0) sceKernelDelayThread(400000); continue; }
        int n = sceUsbCamReadVideoFrameBlocking(jbuf, JPEG_MAX);
        if (n <= 0) {
            if (errors++ < 5) log_msg("camera: frame read %08x", (unsigned)n);
            sceKernelDelayThread(20000);
            if (errors > 100) { log_fail("camera: no frames"); state = CS_FAILED; break; }
            continue;
        }
        errors = 0;
        memcpy(last, jbuf, (size_t)n);
        last_size = n;
        if (rec_on) record_frame(jbuf, n);
        int b = 0;
        while (b == ready || b == reading) b++;
        if (decode(jbuf, n, bufs[b]) == 0) {
            camfilter_apply((uint8_t *)bufs[b], W, H, W, filter, (uint32_t)frames);
            ready = b;
            ready_seq++;
            frames++;
        }
        uint32_t now = platform_ms();
        if (now - t_fps >= 2000) {
            fps = frames * 1000.0f / (float)(now - t_fps);
            log_dbg("camera: %.1f frames/s, last frame %d bytes, recording %d", (double)fps, n, rec_on);
            frames = 0;
            t_fps = now;
        }
        if (now - t_lens >= 500) {
            int l = sceUsbCamGetLensDirection();
            if (l != lens) log_msg("camera: lens %s", l == 1 ? "toward you" : "away");
            lens = l;
            t_lens = now;
        }
    }
    cam_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

static int mic_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    static int16_t buf[MIC_CHUNK / 2] __attribute__((aligned(64)));
    int errors = 0;
    while (!quit) {
        if (!rec_on || !mic_on) { sceKernelDelayThread(20000); continue; }
        int n = sceUsbCamReadMicBlocking(buf, MIC_CHUNK);
        if (n <= 0) {
            if (errors++ < 5) log_msg("camera: mic read %08x", (unsigned)n);
            sceKernelDelayThread(20000);
            continue;
        }
        lock();
        if (rec_on) aviw_audio(&avi, buf, (uint32_t)n & ~1u);
        unlock();
    }
    mic_alive = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

static void release(void) {
    if (video_on) sceUsbCamStopVideo();
    if (mic_on) sceUsbCamStopMic();
    video_on = mic_on = 0;
    if (jpeg_open) { sceJpegDeleteMJpeg(); sceJpegFinishMJpeg(); }
    jpeg_open = 0;
    if (soft) tjDestroy(soft);
    soft = NULL;
    stop_drivers();
    for (int i = 0; i < 3; i++) { free(bufs[i]); bufs[i] = NULL; }
    free(jbuf);
    free(still);
    jbuf = still = NULL;
}

static int connect_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    uint32_t t0 = platform_ms();
    for (int i = 0; i < 3; i++) bufs[i] = memalign(64, W * H * 4);
    jbuf = memalign(64, JPEG_MAX);
    still = memalign(64, STILL_MAX);
    if (!bufs[0] || !bufs[1] || !bufs[2] || !jbuf || !still) { log_fail("camera: no memory"); state = CS_FAILED; goto out; }
    load_modules();
    start_drivers();
    int st = 0;
    while (!quit && platform_ms() - t0 < CONNECT_MS) {
        st = sceUsbGetState();
        if (st & PSP_USB_CONNECTION_ESTABLISHED) break;
        sceKernelDelayThread(100000);
    }
    log_msg("camera: usb state %08x after %u ms", (unsigned)st, (unsigned)(platform_ms() - t0));
    if (!(st & PSP_USB_CONNECTION_ESTABLISHED)) { state = CS_NO_CAMERA; goto out; }
    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    int j1 = sceJpegInitMJpeg(), j2 = sceJpegCreateMJpeg(W, H);
    jpeg_open = j1 >= 0 && j2 >= 0;
    log_msg("camera: sceJpeg init %08x create %08x", (unsigned)j1, (unsigned)j2);
    if (setup_video() != 0) { state = CS_FAILED; goto out; }
    setup_mic();
    if (avi_lock < 0) avi_lock = sceKernelCreateSema("nemo_cam_avi", 0, 1, 1, NULL);
    cam_alive = mic_alive = 1;
    cam_th = sceKernelCreateThread("nemo_cam", cam_thread, 0x14, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (cam_th < 0 || sceKernelStartThread(cam_th, 0, NULL) < 0) { cam_alive = 0; state = CS_FAILED; goto out; }
    mic_th = sceKernelCreateThread("nemo_cam_mic", mic_thread, 0x12, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (mic_th < 0 || sceKernelStartThread(mic_th, 0, NULL) < 0) mic_alive = 0;
    log_msg("camera: ready in %u ms (sceJpeg %s, mic %s)", (unsigned)(platform_ms() - t0), jpeg_open ? "yes" : "no",
            mic_on ? "yes" : "no");
    state = CS_READY;
out:
    sceKernelExitDeleteThread(0);
    return 0;
}

void cam_start(void) {
    if (state == CS_CONNECTING || state == CS_READY) return;
    quit = 0;
    ready = reading = -1;
    ready_seq = taken_seq = 0;
    photo_req = photo_done = 0;
    rec_on = rec_stopped_self = 0;
    state = CS_CONNECTING;
    conn_th = sceKernelCreateThread("nemo_cam_conn", connect_thread, 0x20, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (conn_th < 0 || sceKernelStartThread(conn_th, 0, NULL) < 0) { state = CS_FAILED; log_fail("camera: no thread"); }
}

CamState cam_state(void) { return state; }

void cam_stop(void) {
    if (state == CS_OFF) return;
    while (state == CS_CONNECTING) sceKernelDelayThread(20000);   // the connect thread finishes first
    if (rec_on) cam_record_stop();
    quit = 1;
    for (int i = 0; i < 300 && (cam_alive || mic_alive); i++) sceKernelDelayThread(10000);
    if (cam_alive || mic_alive) log_fail("camera: threads still running at close (cam %d mic %d)", cam_alive, mic_alive);
    else release();   // a thread stuck in a blocking read keeps its buffers: leaked rather than pulled away
    state = CS_OFF;
    log_msg("camera: closed");
}

int cam_take_frame(const uint32_t **px, int *w, int *h) {
    if (ready < 0 || ready_seq == taken_seq) return 0;
    reading = ready;
    taken_seq = ready_seq;
    *px = bufs[reading];
    *w = W;
    *h = H;
    return 1;
}

void cam_frame_done(void) { reading = -1; }
void cam_set_filter(CamFilter f) { filter = f; }
int cam_lens_toward_user(void) { return lens == 1; }
float cam_fps(void) { return fps; }

int cam_zoom(int delta) {
    int z = zoom + delta;
    if (z < 10) z = 10;
    if (z > 40) z = 40;
    if (z != zoom) {
        zoom = z;
        int rc = sceUsbCamSetZoom(z);
        log_dbg("camera: zoom %d rc %08x", z, (unsigned)rc);
    }
    return zoom;
}

void cam_photo(const char *path) {
    snprintf(photo_path, sizeof photo_path, "%s", path);
    photo_done = 0;
    photo_req = 1;
}

int cam_take_photo_result(int *rc) {
    if (!photo_done) return 0;
    photo_done = 0;
    *rc = photo_rc;
    return 1;
}

int cam_record_start(const char *path) {
    lock();
    int rc = aviw_open(&avi, path, W, H, REC_FPS, mic_on ? MIC_RATE : 0, 1);
    if (rc == 0) {
        rec_t0 = platform_ms();
        rec_slots = 0;
        rec_ms = 0;
        rec_stopped_self = 0;
        rec_on = 1;
    }
    unlock();
    log_msg("camera: recording to %s rc %d (sound %s)", path, rc, mic_on ? "22050 Hz mono" : "none");
    return rc;
}

int cam_record_stop(void) {
    lock();
    int rc = -1;
    if (rec_on) {
        rec_on = 0;
        uint32_t frames = avi.n_video, bytes = avi.audio_bytes;
        rc = aviw_close(&avi);
        log_msg("camera: recording stopped: %u frames, %u sound bytes, %u ms, rc %d", (unsigned)frames, (unsigned)bytes,
                (unsigned)rec_ms, rc);
    }
    unlock();
    return rc;
}

int cam_recording(void) { return rec_on; }
uint32_t cam_record_ms(void) { return rec_ms; }

int cam_record_take_stopped(void) {
    if (!rec_stopped_self) return 0;
    rec_stopped_self = 0;
    return 1;
}
