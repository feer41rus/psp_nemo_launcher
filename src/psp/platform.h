#ifndef NEMO_PLATFORM_H
#define NEMO_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define NEMO_DATA_PATH "ms0:/PSP/NEMO/library.json"

typedef struct {
    int battery_pct;    // -1 if no battery
    int charging;
    int wifi_on;
    int hour, minute;
} Status;

void platform_init(void);
void platform_status(Status *s);   // refreshed at most once per second
const char *platform_arkpath(void); // e.g. "ms0:/PSP/SAVEDATA/ARK_01234/"
uint32_t platform_ms(void);

// Main volume via the impose driver (0..30), -1 if unavailable.
int platform_volume_get(void);
// Any impose parameter (PSP_IMPOSE_* of pspimpose_driver.h), -1 if unavailable.
int platform_impose_get(int param);
// ARK's SETTINGS.TXT as text (missing -> ARK's flash1 copy -> ""), NULL if it exists but cannot be
// read (then nothing may be written). Write is crash-safe; 0 ok.
// Files (1.1.10-1.1.11). rmtree: a folder and everything inside (depth-limited); 0 ok.
int  platform_rmtree(const char *path);
int  platform_copy_file(const char *from, const char *to);   // 0 ok
long platform_file_size(const char *path);                    // -1 missing
long platform_dir_size(const char *dir);                      // files directly inside
int  platform_pbp_title(const char *pbp_path, char *out, size_t cap);   // PARAM.SFO TITLE; 0 ok
#define NEMO_ARK_BACKUP "ms0:/PSP/NEMO/ark_settings.bak"
char *platform_ark_read_text(void);
int   platform_ark_write_text(const char *text);
void platform_impose_set(int param, int value);
void platform_volume_set(int volume);

// Any "always, <key>, on|off" line of ARK's SETTINGS.TXT (flash1 copy when missing).
int platform_ark_get(const char *key, int default_on);
int platform_ark_set(const char *key, int on, int default_on);   // 0 ok, -1 error

// Console model (kuKernelGetModel, cached) and firmware string such as "6.61".
int platform_model(void);
const char *platform_fw(void);
// Log free user memory: total and the largest block.
void platform_log_memory(const char *tag);

// Rewrite ARK's SETTINGS.TXT launcher line (only when it differs). 0 ok, -1 error.
int platform_set_launcher_mode(int on);

#endif
