#ifndef NEMO_POWERSAVE_H
#define NEMO_POWERSAVE_H

#include <stdint.h>

// XMB power saving inside Nemo: backlight auto-off and auto sleep with the XMB's own values
// (/CONFIG/SYSTEM/POWER_SAVING, seconds).
extern const int BACKLIGHT_CHOICES[6];   // 0 (off), 10, 30, 60, 300, 600
extern const int SLEEP_CHOICES[7];       // 0 (off), 60, 300, 600, 900, 1800, 3600
int powersave_choice_index(const int *choices, int n, int seconds);   // exact or nearest

typedef struct {
    uint32_t last_ms;    // last button press
    uint32_t busy_ms;    // last moment music / FTP / USB kept the PSP awake
    int off;             // backlight is off because of us
    int slept;           // sleep already requested since the last press
} PowerSave;

typedef struct { int screen_off; int sleep_now; } PowerSaveOut;

void powersave_reset(PowerSave *p, uint32_t now_ms);
int  powersave_press(PowerSave *p, uint32_t now_ms);     // 1 = this press only woke the screen
// busy: music playing / FTP / USB (no sleep; the sleep timer counts from when it ends).
// own_screen: USB / FTP / listening screens switch the display themselves (no backlight-off here).
PowerSaveOut powersave_update(PowerSave *p, uint32_t now_ms, int backlight_s, int sleep_s, int busy, int own_screen);

#endif
