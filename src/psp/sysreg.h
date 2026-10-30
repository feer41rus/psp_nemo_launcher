#ifndef NEMO_SYSREG_H
#define NEMO_SYSREG_H

// System registry (shared with the XMB). get: 0 ok, < 0 error (out untouched). set: 0 ok, < 0 error.
int sysreg_get_int(const char *dir, const char *key, int *out);
int sysreg_set_int(const char *dir, const char *key, int value);
int sysreg_get_str(const char *dir, const char *key, char *out, int cap);   // NUL-terminated
int sysreg_set_str(const char *dir, const char *key, const char *value);

// "Confirm button" (/CONFIG/SYSTEM/XMB button_assign: 1 = cross). get: 1 cross, 0 circle, -1 error.
// Spike 1.1.5: log every key under dir (recursive), skipping passwords and the network profiles.
void sysreg_dump(const char *dir);

// 1.3.12: every key under dir, recursively (sub-categories as SYSREG_DIR). Secret-looking keys (pass, key,
// link, pin, wep, wpa, psk) come with hidden = 1 and no value.
typedef enum { SYSREG_DIR, SYSREG_INT, SYSREG_STR, SYSREG_BIN } SysregKind;
typedef struct {
    const char *path;
    SysregKind kind;
    int hidden, size, ival;
    char sval[64];
    unsigned char bin[16];   // the first bytes of a binary value
} SysregItem;
typedef void (*SysregWalkFn)(void *ctx, const SysregItem *it);
void sysreg_walk(const char *dir, SysregWalkFn cb, void *ctx);

int sysreg_get_confirm_cross(void);
int sysreg_set_confirm_cross(int cross);

#endif
