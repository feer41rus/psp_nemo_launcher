#include "sysreg.h"
#include <pspreg.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "log.h"

static int open_category(const char *dir, REGHANDLE *reg, REGHANDLE *cat) {
    struct RegParam param;
    memset(&param, 0, sizeof param);
    param.regtype = 1;
    strcpy(param.name, "/system");
    param.namelen = strlen(param.name);
    param.unk2 = 1;
    param.unk3 = 1;
    if (sceRegOpenRegistry(&param, 2, reg) < 0) return -1;
    if (sceRegOpenCategory(*reg, dir, 2, cat) < 0) {
        sceRegCloseRegistry(*reg);
        return -1;
    }
    return 0;
}

static void close_category(REGHANDLE reg, REGHANDLE cat, int flush) {
    if (flush) sceRegFlushCategory(cat);
    sceRegCloseCategory(cat);
    if (flush) sceRegFlushRegistry(reg);
    sceRegCloseRegistry(reg);
}

int sysreg_get_int(const char *dir, const char *key, int *out) {
    REGHANDLE reg, cat, k;
    if (open_category(dir, &reg, &cat) != 0) return -1;
    unsigned int type, value = 0;
    SceSize size;
    int ok = sceRegGetKeyInfo(cat, key, &k, &type, &size) >= 0 && sceRegGetKeyValue(cat, k, &value, sizeof value) >= 0;
    close_category(reg, cat, 0);
    if (!ok) return -1;
    *out = (int)value;
    return 0;
}

int sysreg_set_int(const char *dir, const char *key, int value) {
    REGHANDLE reg, cat;
    if (open_category(dir, &reg, &cat) != 0) return -1;
    unsigned int v = (unsigned int)value;
    int rc = sceRegSetKeyValue(cat, key, &v, sizeof v);
    close_category(reg, cat, 1);
    return rc < 0 ? rc : 0;
}

static int has_pass(const char *name) {
    for (const char *p = name; *p; p++)
        if (strncasecmp(p, "pass", 4) == 0) return 1;
    return 0;
}

static void dump_dir(REGHANDLE reg, const char *dir, int depth) {
    if (depth > 4 || strncasecmp(dir, "/CONFIG/NETWORK", 15) == 0) return;   // Wi-Fi keys live there
    REGHANDLE cat;
    if (sceRegOpenCategory(reg, dir, 2, &cat) < 0) { log_msg("reg: %s (cannot open)", dir); return; }
    int num = 0;
    sceRegGetKeysNum(cat, &num);
    if (num > 64) num = 64;
    static char names[64 * 27];
    char local[64 * 27];
    if (num > 0 && sceRegGetKeys(cat, names, num) >= 0) memcpy(local, names, (size_t)num * 27);
    else num = 0;
    for (int i = 0; i < num; i++) {
        char *name = local + i * 27;
        REGHANDLE k;
        unsigned int type = 0;
        SceSize size = 0;
        if (sceRegGetKeyInfo(cat, name, &k, &type, &size) < 0) continue;
        char path[160];
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (has_pass(name)) { log_msg("reg: %s (hidden)", path); continue; }
        if (type == REG_TYPE_DIR) {
            dump_dir(reg, path, depth + 1);
        } else if (type == REG_TYPE_INT) {
            unsigned int v = 0;
            sceRegGetKeyValue(cat, k, &v, sizeof v);
            log_msg("reg: %s = %d", path, (int)v);
        } else if (type == REG_TYPE_STR) {
            char s[128] = { 0 };
            sceRegGetKeyValue(cat, k, s, size < sizeof s ? size : sizeof s - 1);
            log_msg("reg: %s = \"%s\"", path, s);
        } else {
            log_msg("reg: %s (binary %u bytes)", path, (unsigned)size);
        }
    }
    sceRegCloseCategory(cat);
}

void sysreg_dump(const char *dir) {
    struct RegParam param;
    REGHANDLE reg;
    memset(&param, 0, sizeof param);
    param.regtype = 1;
    strcpy(param.name, "/system");
    param.namelen = strlen(param.name);
    param.unk2 = 1;
    param.unk3 = 1;
    if (sceRegOpenRegistry(&param, 2, &reg) < 0) { log_msg("reg: open failed"); return; }
    dump_dir(reg, dir, 0);
    sceRegCloseRegistry(reg);
}

int sysreg_get_str(const char *dir, const char *key, char *out, int cap) {
    REGHANDLE reg, cat, k;
    if (cap < 1 || open_category(dir, &reg, &cat) != 0) return -1;
    unsigned int type;
    SceSize size;
    memset(out, 0, (size_t)cap);
    int ok = sceRegGetKeyInfo(cat, key, &k, &type, &size) >= 0 &&
             sceRegGetKeyValue(cat, k, out, size < (SceSize)cap ? size : (SceSize)cap - 1) >= 0;
    out[cap - 1] = '\0';
    close_category(reg, cat, 0);
    return ok ? 0 : -1;
}

int sysreg_set_str(const char *dir, const char *key, const char *value) {
    REGHANDLE reg, cat;
    if (open_category(dir, &reg, &cat) != 0) return -1;
    int rc = sceRegSetKeyValue(cat, key, value, strlen(value) + 1);
    close_category(reg, cat, 1);
    return rc < 0 ? rc : 0;
}

int sysreg_get_confirm_cross(void) {
    int v;
    if (sysreg_get_int("/CONFIG/SYSTEM/XMB", "button_assign", &v) != 0) return -1;
    return v ? 1 : 0;
}

int sysreg_set_confirm_cross(int cross) { return sysreg_set_int("/CONFIG/SYSTEM/XMB", "button_assign", cross ? 1 : 0); }
