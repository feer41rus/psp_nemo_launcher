#ifndef NEMO_POWER_H
#define NEMO_POWER_H

#include "tags.h"

// Power callback (sleep / wake) and listening-mode controls.
void power_init(void);
int  power_take_suspended(void);   // 1 once after the system started going to sleep
void power_suspend_done(void);     // main thread: state saved, the suspend may continue
int  power_take_resumed(void);     // 1 once after wake-up completed
unsigned power_last_arg(void);     // raw argument of the last power callback (diagnostics)
// Listening mode: lower CPU clock for the format and turn the screen off; off: restore both.
void power_set_listening(int on, AudioFormat fmt);
int  power_screen_is_off(void);
void power_set_screen(int on);     // display only (USB / FTP idle), the clock stays as it is
void power_sleep(void);            // put the PSP to sleep (caller saves state first)
void power_reboot(void);           // full restart of the console (caller saves state first)
void power_shutdown(void);         // turn the console off (caller saves state first)
void power_log_minute(AudioFormat fmt, unsigned underruns);

#endif
