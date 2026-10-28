#include "game_psp.h"
#include <pspctrl.h>
#include <pspintrman_kernel.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspsdk.h>
#include <pspsysmem.h>
#include <string.h>
#include <systemctrl.h>
#include <systemctrl_ark.h>
#include "ccbmp.h"
#include "cclog.h"
#include "ccmenu.h"
#include "ccui.h"

#define IMP_VOLUME 0x1
#define IMP_BRIGHT 0x2
#define IMP_MUTE 0x8
#define DIM_KEEP 96                 // the game frame keeps ~38 % of its brightness behind the window
#define SHOT_DELAY_FRAMES 2         // a clean game frame after the window closed
#define TOAST_US 2000000
#define REPEAT_FIRST_US 400000
#define REPEAT_US 80000

typedef int (*SetFrameBufFn)(void *, int, int, int);
typedef int (*VoidFn)(void);
typedef int (*CtrlFn)(SceCtrlData *, int);
typedef int (*GetParamFn)(int);
typedef int (*SetParamFn)(int, int);
typedef int (*HomePopupFn)(int);
typedef int (*LangModeFn)(int *, int *);

static SetFrameBufFn orig_setfb;
static VoidFn wait_vblank;
static CtrlFn orig_ctrl[4];
static GetParamFn imp_get;
static SetParamFn imp_set;
static LangModeFn lang_mode;
static HomePopupFn home_popup;
static SctrlFunctionPatchData patch_fb, patch_ctrl[4];

static CcMenu menu;
static volatile int block;          // the game sees no buttons (window open, or its buttons still held)
static int shot_frames;             // > 0: frames left before the screenshot
static u64 toast_until;
static const char *toast_text;

// ---------- platform ----------

static u64 now_us(void) { return sceKernelGetSystemTimeWide(); }

int ccgame_buttons(void) {
    SceCtrlData d;
    memset(&d, 0, sizeof d);
    if (orig_ctrl[0]) orig_ctrl[0](&d, 1);   // original PeekBufferPositive with k1 = 0: HOME is visible
    return (int)d.Buttons;
}

void ccgame_block_input(int on) { block = on; }
int ccgame_param(int id) { return imp_get ? imp_get(id) : -1; }
void ccgame_set_param(int id, int v) { if (imp_set) imp_set(id, v); }
void ccgame_vblank(void) { if (wait_vblank) wait_vblank(); }

int ccgame_lang_en(void) {
    int lang = 8, button = 1;
    if (lang_mode) lang_mode(&lang, &button);
    return lang != 8;   // 8 = Russian
}

int ccgame_cross_ok(void) {
    int lang = 8, button = 1;
    if (lang_mode) lang_mode(&lang, &button);
    return button == 1;
}

int ccgame_battery(void) {
    int p = scePowerGetBatteryLifePercent();
    return p >= 0 && p <= 100 ? p : -1;
}

void ccgame_clock(int *h, int *m) {
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) { *h = *m = -1; return; }
    *h = t.hour;
    *m = t.minute;
}

static int exit_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sctrlArkExitLauncher();
    return 0;
}

void ccgame_exit(void) {
    cclog("cc: quit game");
    SceUID th = sceKernelCreateThread("NemoCcExit", exit_thread, 1, 0x1000, 0, NULL);
    if (th < 0) return;
    sceKernelStartThread(th, 0, NULL);
    sceKernelWaitThreadEnd(th, NULL);
    sceKernelDeleteThread(th);
}

// ---------- pad: the game sees nothing while the window is open ----------

static void clear_pad(SceCtrlData *d, int count, int negative) {
    for (int i = 0; i < count; i++) {
        d[i].Buttons = negative ? 0xffffffffu : 0;
        d[i].Lx = d[i].Ly = 128;
    }
}

#define PAD_HOOK(name, idx, neg)                                  \
    static int name(SceCtrlData *d, int count) {                  \
        int rc = orig_ctrl[idx](d, count);                        \
        if (block && rc > 0) clear_pad(d, rc, neg);               \
        return rc;                                                \
    }
PAD_HOOK(peek_pos, 0, 0)
PAD_HOOK(peek_neg, 1, 1)
PAD_HOOK(read_pos, 2, 0)
PAD_HOOK(read_neg, 3, 1)

// ---------- screenshot ----------

// Game ID from the disc (ISO / PSN / UMD); homebrew has none.
static void game_id(char *out, int cap) {
    out[0] = 0;
    SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
    if (fd < 0) return;
    char b[10];
    int n = sceIoRead(fd, b, sizeof b), k = 0;
    sceIoClose(fd);
    for (int i = 0; i < n && b[i] != '|' && k < cap - 1; i++)
        if (b[i] != '-') out[k++] = b[i];
    out[k] = 0;
}

static int save_shot(const CcFrame *fb) {
    static uint8_t rows[8 * 480 * 3];
    char id[16], path[64];
    game_id(id, sizeof id);
    sceIoMkdir("ms0:/PICTURE", 0777);
    sceIoMkdir("ms0:/PICTURE/Nemo", 0777);
    SceIoStat st;
    int n = 1;
    for (; n < 1000; n++) {
        ccbmp_name(path, sizeof path, id, n);
        if (sceIoGetstat(path, &st) < 0) break;
    }
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return -1;
    uint8_t head[54];
    sceIoWrite(fd, head, ccbmp_header(head, fb->w, fb->h));
    int row = fb->w * 3;   // 480 * 3 is a multiple of 4: no padding
    for (int y = fb->h - 1; y >= 0;) {
        int k = 0;
        for (; k < 8 && y >= 0; k++, y--) ccbmp_row(fb, y, rows + k * row);
        sceIoWrite(fd, rows, k * row);
    }
    sceIoClose(fd);
    cclog("cc: screenshot %s", path);
    return 0;
}

// ---------- the window ----------

static void *alloc_block(int size, SceUID *uid) {
    for (int part = 1; part <= 2; part++) {
        *uid = sceKernelAllocPartitionMemory(part, "NemoCcPanel", PSP_SMEM_High, size, NULL);
        if (*uid >= 0) return sceKernelGetBlockHeadAddr(*uid);
    }
    return NULL;
}

static void run_window(CcFrame *fb) {
    int lang_en = ccgame_lang_en(), muted = ccgame_param(IMP_MUTE), volume = ccgame_param(IMP_VOLUME);
    menu.cross_ok = ccgame_cross_ok();
    // The volume is read before the game is muted (muted, the system reports 0) and changes only by our keys.
    ccmenu_open(&menu, volume < 0 ? 0 : volume, ccgame_param(IMP_BRIGHT));
    block = 1;
    ccgame_set_param(IMP_MUTE, 1);
    ccdraw_dim(fb, DIM_KEEP);
    // The panel is composed off screen and copied in one go right after the vblank: drawing straight into the
    // shown frame (erase, then draw) let the screen catch it half done and the window blinked.
    // keep: the dimmed frame under the panel (translucent panel); work: the composed panel.
    int size = CCUI_W * CCUI_H * ccpix_bpp(fb->fmt);
    SceUID keep_uid = -1, work_uid = -1;
    void *keep = alloc_block(size, &keep_uid), *work = keep ? alloc_block(size, &work_uid) : NULL;
    if (keep && !work) {   // memory for one buffer only: opaque panel composed in it
        work = keep;
        work_uid = keep_uid;
        keep = NULL;
        keep_uid = -1;
    }
    if (keep) ccdraw_save(fb, CCUI_X, CCUI_Y, CCUI_W, CCUI_H, keep);
    CcFrame wf = { work, CCUI_W, CCUI_W, CCUI_H, fb->fmt };
    cclog("cc: open fmt=%d width=%d translucent=%d buffered=%d", fb->fmt, fb->stride, keep != NULL, work != NULL);
    int prev = ccgame_buttons(), hour = -1, minute = -1, battery = ccgame_battery(), dirty = 1;
    u64 held_since = 0, last_repeat = 0;
    CcAction act = CCA_NONE;
    while (menu.open) {
        int h, mi;
        ccgame_clock(&h, &mi);
        if (h != hour || mi != minute) { hour = h; minute = mi; dirty = 1; }
        if (dirty && work) {
            if (keep) memcpy(work, keep, size);
            ccui_draw(&wf, 0, 0, &menu, lang_en, keep != NULL, hour, minute, battery);
        }
        ccgame_vblank();
        if (dirty) {
            if (work) ccdraw_restore(fb, CCUI_X, CCUI_Y, CCUI_W, CCUI_H, work);
            else ccui_draw(fb, CCUI_X, CCUI_Y, &menu, lang_en, 0, hour, minute, battery);
            dirty = 0;
        }
        int b = ccgame_buttons(), pressed = b & ~prev;
        u64 t = now_us();
        int arrows = b & (CC_LEFT | CC_RIGHT | CC_VOLUP | CC_VOLDOWN);   // held ←→ and VOL± repeat
        if (!arrows) held_since = 0;
        else if (pressed & arrows) held_since = last_repeat = t;
        else if (t - held_since > REPEAT_FIRST_US && t - last_repeat > REPEAT_US) {
            pressed |= arrows;
            last_repeat = t;
        }
        prev = b;
        if (pressed) dirty = 1;
        act = ccmenu_input(&menu, (uint32_t)pressed);
        // The volume is applied when the window closes: setting it now made the system unmute the game
        // a moment later (the game's music came through). Keep the game muted meanwhile.
        if (ccgame_param(IMP_MUTE) == 0) ccgame_set_param(IMP_MUTE, 1);
        if (act == CCA_BRIGHT) ccgame_set_param(IMP_BRIGHT, menu.bright);
    }
    ccgame_set_param(IMP_VOLUME, menu.volume);   // also undoes the system's own step on VOL± while muted
    if (keep_uid >= 0) sceKernelFreePartitionMemory(keep_uid);
    if (work_uid >= 0) sceKernelFreePartitionMemory(work_uid);
    ccgame_set_param(IMP_MUTE, muted > 0 ? muted : 0);
    cclog("cc: close action=%d volume=%d", act, menu.volume);
    if (act == CCA_SHOT) shot_frames = SHOT_DELAY_FRAMES;
    if (act == CCA_EXIT) ccgame_exit();
}

// Every frame the game shows passes here (after the original): HOME, screenshot, toast.
static int setfb_hook(void *top, int width, int fmt, int sync) {
    // While the window is open the screen stays on its frame: the game (often from an interrupt) would
    // otherwise keep flipping to its other buffer and the window blinks.
    if (menu.open) return 0;
    CcFrame fb = { (void *)((u32)top | 0x40000000), width, 480, 272, (CcPixFmt)fmt };   // uncached view
    // The note goes into the finished frame before it is shown: drawn after, the game's still running
    // drawing overwrote the thin letters and only the dark plate stayed.
    if (top && fmt >= 0 && fmt <= 3 && toast_text && now_us() < toast_until) ccui_toast(&fb, toast_text);
    int rc = orig_setfb(top, width, fmt, sync);
    if (!top || sceKernelIsIntrContext() || fmt < 0 || fmt > 3) return rc;
    int k1 = pspSdkSetK1(0);
    int b = ccgame_buttons();
    static int frames;
    // Keep the Sony HOME window off: the system may turn it back on after the game started.
    if (home_popup && ((b & CC_HOME) || ++frames >= 60)) {
        home_popup(0);
        frames = 0;
    }
    if (block && !menu.open && !(b & 0xffff)) block = 0;   // the game gets buttons back once all are released
    if (ccmenu_home(&menu, (b & CC_HOME) != 0, 1)) run_window(&fb);
    if (shot_frames > 0 && --shot_frames == 0) {
        int lang_en = ccgame_lang_en();
        toast_text = save_shot(&fb) == 0 ? ccmenu_text(CCT_SAVED, lang_en) : NULL;
        toast_until = now_us() + TOAST_US;
    }
    pspSdkSetK1(k1);
    return rc;
}

// ---------- install ----------

static void *find(const char *mod, const char *lib, u32 nid) { return (void *)sctrlHENFindFunction(mod, lib, nid); }

int ccgame_install(void) {
    void *setfb = find("sceDisplay_Service", "sceDisplay_driver", 0xA38B3F89);
    wait_vblank = (VoidFn)find("sceDisplay_Service", "sceDisplay_driver", 0xB0942511);
    static const u32 CTRL_NIDS[4] = { 0x2BA616AF, 0xE6085C33, 0xBE30CED0, 0x3A6A612A };
    static void *const HOOKS[4] = { (void *)peek_pos, (void *)peek_neg, (void *)read_pos, (void *)read_neg };
    imp_get = (GetParamFn)find("sceImpose_Driver", "sceImpose_driver", 0xDC3BECFF);
    imp_set = (SetParamFn)find("sceImpose_Driver", "sceImpose_driver", 0x3C318569);
    lang_mode = (LangModeFn)find("sceImpose_Driver", "sceImpose_driver", 0x842309DD);
    HomePopupFn popup = home_popup = (HomePopupFn)find("sceImpose_Driver", "sceImpose_driver", 0xC08C41EF);
    void *ctrl[4];
    for (int i = 0; i < 4; i++) ctrl[i] = find("sceController_Service", "sceCtrl_driver", CTRL_NIDS[i]);
    if (!setfb || !wait_vblank || !ctrl[0] || !ctrl[1] || !ctrl[2] || !ctrl[3]) {
        cclog("cc: functions not found (setfb=%p vblank=%p)", setfb, wait_vblank);
        return -1;
    }
    ccmenu_init(&menu, 1);
    for (int i = 0; i < 4; i++) sctrlHENHijackFunction(&patch_ctrl[i], ctrl[i], HOOKS[i], (void **)&orig_ctrl[i]);
    sctrlHENHijackFunction(&patch_fb, setfb, (void *)setfb_hook, (void **)&orig_setfb);
    sctrlFlushCache();
    if (popup) popup(0);   // no Sony HOME window: ours instead
    cclog("cc: installed (popup=%p impose=%p/%p)", popup, imp_get, imp_set);
    return 0;
}
