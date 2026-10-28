// Control Center (1.2.4): Nemo carries the plugin inside VBOOT.PBP, writes it to the memory stick and keeps
// its line in ARK's PLUGINS.TXT in step with Settings › System › Nemo › Control Center.
#include "ccplug.h"
#include "log.h"
#include "platform.h"
#include "safefile.h"
#include "ui.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEMO_NO_CC
extern const unsigned char nemo_cc[];
extern const unsigned int nemo_cc_size;

// The plugin file on the stick equals the one inside Nemo: 1.
static int same_file(void) {
    SceIoStat st;
    if (sceIoGetstat(CC_PLUGIN_PATH, &st) < 0 || st.st_size != (SceOff)nemo_cc_size) return 0;
    unsigned char *b = malloc(nemo_cc_size);
    if (!b) return 0;
    FILE *f = fopen(CC_PLUGIN_PATH, "rb");
    int same = f && fread(b, 1, nemo_cc_size, f) == nemo_cc_size && memcmp(b, nemo_cc, nemo_cc_size) == 0;
    if (f) fclose(f);
    free(b);
    return same;
}

static int write_plugin(void) {
    if (same_file()) return 0;
    sceIoMkdir("ms0:/PSP/NEMO", 0777);
    FILE *f = fopen(CC_PLUGIN_PATH, "wb");
    int ok = f && fwrite(nemo_cc, 1, nemo_cc_size, f) == nemo_cc_size;
    if (f) fclose(f);
    log_msg("cc: plugin written (%u bytes) ok=%d", nemo_cc_size, ok);
    return ok ? 0 : -1;
}
#endif

void cc_deploy(App *a) {
    int want = a->lib->settings.control_center;
#ifdef NEMO_NO_CC
    want = 0;                                   // a build without the plugin switches an old line off
#else
    if (want && write_plugin() != 0) want = 0;
#endif
    char path[160];
    snprintf(path, sizeof path, "%sPLUGINS.TXT", platform_arkpath());
    int missing = 0;
    char *t = safe_read_text(path, 64 * 1024, &missing);
    if (!t && !missing) {
        log_msg("cc: %s unreadable, left as it is", path);
        return;
    }
    PluginFile *pf = malloc(sizeof *pf);
    if (!pf) { free(t); return; }
    plugins_parse(t ? t : "", pf);
    free(t);
    if (ccplug_sync(pf, CC_PLUGIN_PATH, want)) {
        char *out = plugins_render(pf);
        int rc = out ? safe_write_text(path, out) : -1;
        free(out);
        log_msg("cc: %s line %s rc=%d", path, want ? "on" : "off", rc);
    }
    free(pf);
}
