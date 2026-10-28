#include "ccplug.h"
#include <strings.h>

static PluginLine *find_line(PluginFile *f, const char *path) {
    for (int i = 0; i < f->count; i++)
        if (!f->lines[i].custom && !f->lines[i].removed && strcasecmp(f->lines[i].path, path) == 0) return &f->lines[i];
    return 0;
}

int ccplug_sync(PluginFile *f, const char *path, int want_on) {
    PluginLine *l = find_line(f, path);
    if (want_on) {
        if (!l) return plugins_add(f, path, CC_PLUGIN_LEVEL) == 0;
        if (plugins_level_index(l) == CC_PLUGIN_LEVEL) return 0;
        plugins_set_level(l, CC_PLUGIN_LEVEL);
        return 1;
    }
    if (!l || plugins_level_index(l) == 0) return 0;
    plugins_set_level(l, 0);
    return 1;
}
