#include "arkopts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const ArkOpt ARK_OPT_USBCHARGE = { "usbcharge", 2, { { "usbcharge", 0 }, { "usbcharge", 1 } }, 1 };
const ArkOpt ARK_OPT_HIGHMEM = { "highmem", 5,
    { { "highmem", 0 }, { "highmem", 1 }, { "highmem:auto", 1 }, { "highmem:force16", 1 }, { "highmem:forcemax", 1 } }, 1 };
const ArkOpt ARK_OPT_MSCACHE = { "mscache", 4,
    { { "mscache", 0 }, { "mscache:4k", 1 }, { "mscache:8k", 1 }, { "mscache:16k", 1 } }, 1 };
const ArkOpt ARK_OPT_INFERNO = { "infernocache", 3,
    { { "infernocache", 0 }, { "infernocache:lru", 1 }, { "infernocache:rr", 1 } }, 1 };
const ArkOpt ARK_OPT_SKIPLOGOS = { "skiplogos", 4,
    { { "skiplogos", 0 }, { "skiplogos", 1 }, { "skiplogos:gameboot", 1 }, { "skiplogos:coldboot", 1 } }, 1 };
const ArkOpt ARK_OPT_NOLED = { "noled", 2, { { "noled", 0 }, { "noled", 1 } }, 1 };
const ArkOpt ARK_OPT_NOUMD = { "noumd", 2, { { "noumd", 0 }, { "noumd", 1 } }, 1 };
const ArkOpt ARK_OPT_NOANALOG = { "noanalog", 2, { { "noanalog", 0 }, { "noanalog", 1 } }, 1 };
const ArkOpt ARK_OPT_REGION = { "region_", 4,
    { { NULL, 0 }, { "region_us", 1 }, { "region_eu", 1 }, { "region_jp", 1 } }, -1 };

const int ARK_CLOCKS[8] = { 0, 133, 222, 333, 383, 403, 423, 443 };

typedef struct { char scope[48], key[48], value[8]; } Line;

static void trim_copy(char *dst, size_t cap, const char *b, const char *e) {
    while (b < e && (*b == ' ' || *b == '\t')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    size_t n = (size_t)(e - b) < cap - 1 ? (size_t)(e - b) : cap - 1;
    memcpy(dst, b, n);
    dst[n] = '\0';
}

// "scope, key, value" (comments and other shapes -> 0).
static int parse(const char *line, const char *end, Line *out) {
    if (line < end && *line == '#') return 0;
    const char *c1 = memchr(line, ',', (size_t)(end - line));
    if (!c1) return 0;
    const char *c2 = memchr(c1 + 1, ',', (size_t)(end - c1 - 1));
    if (!c2) return 0;
    trim_copy(out->scope, sizeof out->scope, line, c1);
    trim_copy(out->key, sizeof out->key, c1 + 1, c2);
    trim_copy(out->value, sizeof out->value, c2 + 1, end);
    return 1;
}

static int is_level(const char *scope) {
    static const char *const LEVELS[] = { "always", "game", "vsh", "launcher", "umd", "homebrew", "pops" };
    for (size_t i = 0; i < sizeof LEVELS / sizeof *LEVELS; i++)
        if (strcasecmp(scope, LEVELS[i]) == 0) return 1;
    return 0;
}

static int in_family(const char *key, const char *family) {
    size_t n = strlen(family);
    if (strncasecmp(key, family, n) != 0) return 0;
    return family[n - 1] == '_' || key[n] == '\0' || key[n] == ':';
}

// Copies text without the lines drop() selects, then appends extra lines ("a\nb\n" form, may be "").
static char *rebuild(const char *text, int (*drop)(const Line *, const void *), const void *ctx, const char *extra) {
    const char *eol = strstr(text, "\r\n") ? "\r\n" : "\n";
    size_t cap = strlen(text) + strlen(extra) * 2 + 4;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t o = 0;
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl + 1 : line + strlen(line);
        Line l;
        if (!(parse(line, nl ? nl : end, &l) && drop(&l, ctx))) {
            memcpy(out + o, line, (size_t)(end - line));
            o += (size_t)(end - line);
        }
        line = end;
    }
    if (*extra && o > 0 && out[o - 1] != '\n') { memcpy(out + o, eol, strlen(eol)); o += strlen(eol); }
    for (const char *p = extra; *p; p++) {       // extra uses "\n": convert to the file's line ends
        if (*p == '\n') { memcpy(out + o, eol, strlen(eol)); o += strlen(eol); }
        else out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

static int each_line(const char *text, void (*fn)(const Line *, void *), void *ctx) {
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);
        Line l;
        if (parse(line, end, &l)) fn(&l, ctx);
        if (!nl) break;
        line = nl + 1;
    }
    return 0;
}

typedef struct { const ArkOpt *o; int choice; } GetCtx;

static void get_line(const Line *l, void *p) {
    GetCtx *g = p;
    if (strcasecmp(l->scope, "always") != 0 || !in_family(l->key, g->o->family)) return;
    if (strcasecmp(l->value, "on") != 0) { g->choice = 0; return; }
    for (int i = 0; i < g->o->count; i++)
        if (g->o->choices[i].key && g->o->choices[i].on && strcasecmp(g->o->choices[i].key, l->key) == 0) {
            g->choice = i;
            return;
        }
    if (g->o->plain_on >= 0 && strcasecmp(l->key, g->o->family) == 0) g->choice = g->o->plain_on;
}

int arkopt_get(const char *text, const ArkOpt *o) {
    GetCtx g = { o, 0 };
    each_line(text, get_line, &g);
    return g.choice;
}

static int drop_family(const Line *l, const void *ctx) {
    const ArkOpt *o = ctx;
    return is_level(l->scope) && in_family(l->key, o->family);
}

char *arkopt_set(const char *text, const ArkOpt *o, int choice) {
    char extra[96] = "";
    if (choice >= 0 && choice < o->count && o->choices[choice].key)
        snprintf(extra, sizeof extra, "always, %s, %s\n", o->choices[choice].key, o->choices[choice].on ? "on" : "off");
    return rebuild(text, drop_family, o, extra);
}

static const char *const CLOCK_SCOPES[3] = { "game", "vsh", "launcher" };

static int clock_mhz(const char *key) {
    if (strncasecmp(key, "cpuclock:", 9) == 0) return atoi(key + 9);
    if (strcasecmp(key, "overclock") == 0) return 333;
    if (strcasecmp(key, "defaultclock") == 0) return 222;
    if (strcasecmp(key, "powersave") == 0) return 133;
    return -1;
}

typedef struct { ClockTarget t; int mhz; } ClkCtx;

static void clock_line(const Line *l, void *p) {
    ClkCtx *c = p;
    if (strcasecmp(l->scope, "always") != 0 && strcasecmp(l->scope, CLOCK_SCOPES[c->t]) != 0) return;
    int mhz = clock_mhz(l->key);
    if (mhz < 0) return;
    c->mhz = strcasecmp(l->value, "on") == 0 ? mhz : 0;
}

int arkclock_get(const char *text, ClockTarget t) {
    ClkCtx c = { t, 0 };
    each_line(text, clock_line, &c);
    return c.mhz;
}

static int drop_clock(const Line *l, const void *ctx) {
    (void)ctx;
    if (clock_mhz(l->key) < 0) return 0;
    return strcasecmp(l->scope, "always") == 0 || strcasecmp(l->scope, "game") == 0 ||
           strcasecmp(l->scope, "vsh") == 0 || strcasecmp(l->scope, "launcher") == 0;
}

char *arkclock_set(const char *text, ClockTarget t, int mhz) {
    int v[3];
    for (int i = 0; i < 3; i++) v[i] = arkclock_get(text, (ClockTarget)i);
    v[t] = mhz;
    char extra[160] = "";
    if (v[0] && v[0] == v[1] && v[1] == v[2]) {      // one line for all, like ARK's app
        snprintf(extra, sizeof extra, "always, cpuclock:%d, on\n", v[0]);
    } else {
        size_t n = 0;
        for (int i = 0; i < 3; i++)
            if (v[i]) n += (size_t)snprintf(extra + n, sizeof extra - n, "%s, cpuclock:%d, on\n", CLOCK_SCOPES[i], v[i]);
    }
    return rebuild(text, drop_clock, NULL, extra);
}

const char ARK_FACTORY_SETTINGS[] =
    "always, usbcharge, on\n"
    "always, cpuclock:333, on\n"
    "always, wpa2, on\n"
    "always, launcher, off\n"
    "always, highmem, off\n"
    "always, mscache, on\n"
    "always, infernocache, on\n"
    "always, disablepause, off\n"
    "always, hibblock, on\n"
    "always, oldplugin, on\n"
    "always, skiplogos, off\n"
    "always, hidepics, off\n"
    "always, hidemac, on\n"
    "always, hidedlc, on\n"
    "always, noled, off\n"
    "always, noumd, off\n"
    "always, noanalog, off\n"
    "always, vitmute, on\n"
    "always, qaflags, on\n"
    "\n"
    "# The following games don't like Inferno Cache\n"
    "# Luxor - The Wrath of Set (the other Luxor game works fine)\n"
    "ULUS10201, infernocache, off\n"
    "# Flat-Out Head On (both US and EU)\n"
    "ULUS10328 ULES00968, infernocache, off\n"
    "\n";

static int factory_has(const char *line, size_t len) {
    const char *p = ARK_FACTORY_SETTINGS;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n == len && strncmp(p, line, len) == 0) return 1;
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

char *arkopts_factory_reset(const char *current) {
    size_t cap = sizeof ARK_FACTORY_SETTINGS + strlen(current) + 2;
    char *out = malloc(cap);
    if (!out) return NULL;
    strcpy(out, ARK_FACTORY_SETTINGS);
    size_t o = strlen(out);
    const char *line = current;
    while (*line) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);
        size_t len = (size_t)(end - line);
        if (len && line[len - 1] == '\r') len--;
        Line l;
        if (parse(line, line + len, &l) && !is_level(l.scope) && !factory_has(line, len)) {
            memcpy(out + o, line, len);           // a per-game line the user added
            o += len;
            out[o++] = '\n';
        }
        if (!nl) break;
        line = nl + 1;
    }
    out[o] = '\0';
    return out;
}

typedef struct { const char *id; int mhz; } GameClkCtx;

static void game_clock_line(const Line *l, void *p) {
    GameClkCtx *c = p;
    int mhz = clock_mhz(l->key);
    if (mhz < 0 || strcasecmp(l->scope, c->id) != 0) return;
    c->mhz = strcasecmp(l->value, "on") == 0 ? mhz : 0;
}

int arkclock_game_get(const char *text, const char *game_id) {
    GameClkCtx c = { game_id, 0 };
    each_line(text, game_clock_line, &c);
    return c.mhz;
}

static int drop_game_clock(const Line *l, const void *ctx) {
    return clock_mhz(l->key) >= 0 && strcasecmp(l->scope, (const char *)ctx) == 0;
}

char *arkclock_game_set(const char *text, const char *game_id, int mhz) {
    char extra[96] = "";
    if (mhz) snprintf(extra, sizeof extra, "%s, cpuclock:%d, on\n", game_id, mhz);
    return rebuild(text, drop_game_clock, game_id, extra);
}
