#ifndef NEMO_CCPLUG_H
#define NEMO_CCPLUG_H

#include "plugins.h"

// Nemo installs its Control Center plugin itself (1.2.4) and keeps its line in ARK's PLUGINS.TXT in step with
// Settings › System › Nemo › Control Center.
#define CC_PLUGIN_PATH "ms0:/PSP/NEMO/nemo_cc.prx"
#define CC_PLUGIN_LEVEL 2   // "game": PSP games and homebrew (PLUGIN_LEVELS)

// want_on: the line exists and runs at "game"; off: an existing line is switched off (kept). 1 = changed.
int ccplug_sync(PluginFile *f, const char *path, int want_on);

#endif
