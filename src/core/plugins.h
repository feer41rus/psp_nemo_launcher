#ifndef NEMO_PLUGINS_H
#define NEMO_PLUGINS_H

#include <stddef.h>

// ARK's PLUGINS.TXT ("<where>, <path>, on|off"), read and written like ARK's plugin manager
// (XMBControl plugins.c). Comments and other lines are kept as they are.
#define PLUGINS_MAX 64

typedef struct {
    char text[256];       // custom lines: written back verbatim
    char runlevel[64];    // always / game / umd / homebrew / pops / vsh / launcher, or game IDs
    char path[160];
    int on;
    int custom;           // comment / blank / not "a, b, c"
    int removed;
} PluginLine;

typedef struct {
    PluginLine lines[PLUGINS_MAX];
    int count;
} PluginFile;

#define PLUGIN_LEVEL_COUNT 8
// Index 0 = off (the level is kept); 1..7 = always, game, umd, homebrew, pops, vsh, launcher.
extern const char *const PLUGIN_LEVELS[PLUGIN_LEVEL_COUNT];

void plugins_parse(const char *text, PluginFile *f);
char *plugins_render(const PluginFile *f);                    // malloc'd
int  plugins_level_index(const PluginLine *l);                // 0 off, 1..7, -1 = per-game line (on or off)
void plugins_set_level(PluginLine *l, int index);
const char *plugins_name(const PluginLine *l, char *out, size_t cap);   // file name of the path
int  plugins_add(PluginFile *f, const char *path, int level_index);     // 0 ok, -1 full
int  plugins_has(const PluginFile *f, const char *file_name);           // listed (not removed), any case
int  plugins_blacklisted(const char *file_name);              // ARK's own modules: never offered

#endif
