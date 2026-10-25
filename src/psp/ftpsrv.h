#ifndef NEMO_FTPSRV_H
#define NEMO_FTPSRV_H

#include <stdint.h>

// FTP server over the vendored libpspftp: user "Psp-nemo", a new 6-character password every time it
// is switched on, root ms0:/. Needs Wi-Fi with an IP; waits for it when switched on without one.
typedef enum { FTP_OFF, FTP_WAIT_WIFI, FTP_ON } FtpPhase;

typedef struct {
    FtpPhase phase;
    char password[7];
    char client_ip[16];      // "" when no PC is connected
    uint64_t bytes;          // transferred since it was switched on
} FtpStatus;

#define FTP_USER "Psp-nemo"

void ftpsrv_enable(int wifi_connected);   // new password; starts now or when Wi-Fi gets an IP
void ftpsrv_disable(void);
// Every frame: starts a waiting server once connected; switches off when Wi-Fi is lost.
// Returns 1 once when it switched itself off.
int  ftpsrv_tick(int wifi_connected);
void ftpsrv_status(FtpStatus *out);

#endif
