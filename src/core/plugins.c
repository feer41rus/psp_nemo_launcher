#include "plugins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const PLUGIN_LEVELS[PLUGIN_LEVEL_COUNT] = {
    NULL, "always", "game", "umd", "homebrew", "pops", "vsh", "launcher",
};

// ARK-5 XMBControl plugins.c plugin_blacklist.
static const char *const BLACKLIST[] = {
    "popscore.prx", "pops_bridge.prx", "usbhostfs.prx", "IDSREG.PRX", "INTRAFON.PRX", "LIBPNG.PRX",
    "MEDIASYN.PRX", "POPS.PRX", "POPSMAN.PRX", "PS1SPU.PRX", "PSPAV.PRX", "PSPFTP.PRX", "RECOVERY.PRX",
    "UNARCH.PRX", "USBDEV.PRX", "VLF.PRX", "VSHMENU.PRX", "XMBCTRL.PRX",
};

static void trim_copy(char *dst, size_t cap, const char *b, const char *e) {
    while (b < e && (*b == ' ' || *b == '\t')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    size_t n = (size_t)(e - b) < cap - 1 ? (size_t)(e - b) : cap - 1;
    memcpy(dst, b, n);
    dst[n] = '\0';
}

void plugins_parse(const char *text, PluginFile *f) {
    memset(f, 0, sizeof *f);
    const char *line = text;
    while (*line && f->count < PLUGINS_MAX) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);
        PluginLine *l = &f->lines[f->count];
        const char *c1 = memchr(line, ',', (size_t)(end - line));
        const char *c2 = c1 ? memchr(c1 + 1, ',', (size_t)(end - c1 - 1)) : NULL;
        if (*line == '#' || !c2) {
            l->custom = 1;
            trim_copy(l->text, sizeof l->text, line, end);
        } else {
            char v[16];
            trim_copy(l->runlevel, sizeof l->runlevel, line, c1);
            trim_copy(l->path, sizeof l->path, c1 + 1, c2);
            trim_copy(v, sizeof v, c2 + 1, end);
            l->on = strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0;
        }
        if (!(l->custom && !l->text[0] && !nl)) f->count++;   // no phantom empty line at the end
        if (!nl) break;
        line = nl + 1;
    }
}

char *plugins_render(const PluginFile *f) {
    char *out = malloc((size_t)f->count * 300 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (int i = 0; i < f->count; i++) {
        const PluginLine *l = &f->lines[i];
        if (l->removed) continue;
        if (l->custom) o += (size_t)sprintf(out + o, "%s\n", l->text);
        else o += (size_t)sprintf(out + o, "%s, %s, %s\n", l->runlevel, l->path, l->on ? "on" : "off");
    }
    out[o] = '\0';
    return out;
}

int plugins_level_index(const PluginLine *l) {
    for (int i = 1; i < PLUGIN_LEVEL_COUNT; i++)
        if (strcasecmp(l->runlevel, PLUGIN_LEVELS[i]) == 0) return l->on ? i : 0;
    return -1;                                   // game IDs: shown as such, on or off
}

void plugins_set_level(PluginLine *l, int index) {
    if (index <= 0) { l->on = 0; return; }
    snprintf(l->runlevel, sizeof l->runlevel, "%s", PLUGIN_LEVELS[index]);
    l->on = 1;
}

const char *plugins_name(const PluginLine *l, char *out, size_t cap) {
    const char *slash = strrchr(l->path, '/');
    const char *colon = strrchr(l->path, ':');
    const char *n = slash ? slash + 1 : colon ? colon + 1 : l->path;
    snprintf(out, cap, "%s", n);
    return out;
}

int plugins_add(PluginFile *f, const char *path, int level_index) {
    if (f->count >= PLUGINS_MAX) return -1;
    PluginLine *l = &f->lines[f->count++];
    memset(l, 0, sizeof *l);
    snprintf(l->path, sizeof l->path, "%s", path);
    snprintf(l->runlevel, sizeof l->runlevel, "%s", PLUGIN_LEVELS[1]);
    plugins_set_level(l, level_index);
    return 0;
}

int plugins_has(const PluginFile *f, const char *file_name) {
    char n[160];
    for (int i = 0; i < f->count; i++)
        if (!f->lines[i].custom && !f->lines[i].removed &&
            strcasecmp(plugins_name(&f->lines[i], n, sizeof n), file_name) == 0)
            return 1;
    return 0;
}

int plugins_blacklisted(const char *file_name) {
    for (size_t i = 0; i < sizeof BLACKLIST / sizeof *BLACKLIST; i++)
        if (strcasecmp(file_name, BLACKLIST[i]) == 0) return 1;
    return 0;
}
