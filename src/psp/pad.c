#include "pad.h"
#include <pspctrl.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <string.h>

static void *kernel_read;   // sceCtrlReadBufferPositive from the driver (sees HOME/NOTE)

void pad_init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    // Same approach as arkMenu/src/controller.cpp.
    kernel_read = (void *)sctrlHENFindFunction("sceController_Service", "sceCtrl_driver", 0x1F803938);
}

uint32_t pad_read(uint8_t *lx, uint8_t *ly) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    if (kernel_read) {
        struct KernelCallArg args;
        memset(&args, 0, sizeof args);
        args.arg1 = (u32)&pad;
        args.arg2 = 1;
        kuKernelCall(kernel_read, &args);
    } else {
        sceCtrlReadBufferPositive(&pad, 1);
    }
    if (lx) *lx = pad.Lx;
    if (ly) *ly = pad.Ly;
    return pad.Buttons;
}
