// Nemo Control Center (1.2.4): ARK plugin for PSP games and homebrew. HOME opens Nemo's window instead of the
// Sony one. Inspired by CustomHOME (ABCanG) and Custom Home Mod (suloku); no code of theirs is used.
#include <pspkernel.h>
#include <pspinit.h>
#include <string.h>
#include "cclog.h"
#include "game_psp.h"

PSP_MODULE_INFO("NemoControlCenter", 0x1000, 1, 0);

// Nemo itself (VBOOT.PBP, also when started from the XMB as homebrew): the plugin stays idle there.
static int in_launcher(const char *init) {
    size_t n = init ? strlen(init) : 0;
    return n >= 10 && strcmp(init + n - 10, "/VBOOT.PBP") == 0;
}

int module_start(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    const char *init = sceKernelInitFileName();
    if (in_launcher(init)) {
        cclog("cc: in launcher, idle");
        return 0;
    }
    int rc = ccgame_install();
    cclog("cc: start %s, install rc=%d", init ? init : "?", rc);
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    return 0;
}
