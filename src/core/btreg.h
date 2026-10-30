#ifndef NEMO_BTREG_H
#define NEMO_BTREG_H

// Bluetooth on the PSP Go (1.3.12), shown only. There is no public API: the state is looked for in the
// system registry. Where Sony keeps it is not known yet, so the keys are guessed by name and the detailed
// log lists every Bluetooth-looking entry; the guesses are replaced by the real names after a PSP Go log.
typedef enum { REG_INT, REG_STR, REG_BIN } RegType;

typedef struct {
    char path[96];   // "/CONFIG/BT/bt_enable"
    RegType type;
    int ival;
    char sval[64];
    int size;
} RegEntry;

#define BT_MAX_PAIRED 4

typedef struct {
    int found;            // any Bluetooth entry in the registry
    int enabled;          // 1 on, 0 off, -1 unknown
    int connected;        // 1 a device is connected, 0 none, -1 unknown
    char connected_name[64];
    int paired;
    char paired_names[BT_MAX_PAIRED][48];
} BtState;

int  btreg_is_bt_path(const char *path);   // a path segment "BT", "BT_…", "Bt…" or one containing "BLUETOOTH"
void btreg_scan(const RegEntry *e, int n, BtState *out);

#endif
