#include "ftpsrv.h"
#include "log.h"
#include "netmodel.h"
#include <pspkernel.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "ftp.h"
#include "ftpd.h"
#include "psp_cfg.h"

static FtpPhase phase = FTP_OFF;
static char password[7];
static volatile char client_ip[16];
static unsigned long long bytes_at_start;
static SceUID thread = -1;
static mftpUser_t user;

// libpspftp reports through messages; "Connection from a.b.c.d" / "Client disconnected" (Nemo patch).
static void on_message(const char *msg) {
    if (strncmp(msg, "Connection from ", 16) == 0) {
        snprintf((char *)client_ip, sizeof client_ip, "%.15s", msg + 16);
        log_msg("ftp: client %s", msg + 16);
    } else if (strcmp(msg, "Client disconnected") == 0) {
        ((char *)client_ip)[0] = '\0';
        log_msg("ftp: client gone");
    } else if (strncasecmp(msg, "> PASS", 6) == 0) {
        log_msg("ftp: > PASS ****");                 // never log the password
    } else {
        log_msg("ftp: %s", msg);                     // "> COMMAND from ip" and server notices
    }
}

static void start_server(void) {
    memset(&user, 0, sizeof user);
    snprintf(user.user, sizeof user.user, "%s", FTP_USER);
    snprintf(user.password, sizeof user.password, "%s", password);
    snprintf(user.root, sizeof user.root, "ms0:");
    mftp_config.head_user = &user;
    mftp_config.auth_required = 1;
    ftpdSetDevice("ms0:");
    ftpdSetMsgHandler(on_message);
    ((char *)client_ip)[0] = '\0';
    bytes_at_start = mftp_bytes;
    thread = sceKernelCreateThread("nemo_ftpd", ftpdLoop, 0x18, 0x4000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_USBWLAN,
                                   NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
    phase = FTP_ON;
    log_msg("ftp: server started thread=%08X", (unsigned)thread);   // never log the password
}

static void stop_server(void) {
    if (thread < 0) return;
    ftpdExitHandler(0, NULL);   // closes the listening socket, terminates client threads
    SceUInt timeout = 2000 * 1000;
    if (sceKernelWaitThreadEnd(thread, &timeout) < 0) sceKernelTerminateThread(thread);
    sceKernelDeleteThread(thread);
    thread = -1;
    ((char *)client_ip)[0] = '\0';
    log_msg("ftp: server stopped, %llu bytes transferred", mftp_bytes - bytes_at_start);
}

void ftpsrv_enable(int wifi_connected) {
    if (phase != FTP_OFF) return;
    ftp_password((uint32_t)sceKernelGetSystemTimeLow() ^ 0x5eed, password);
    phase = FTP_WAIT_WIFI;
    log_msg("ftp: enabled (wifi %s)", wifi_connected ? "connected" : "not connected yet");
    if (wifi_connected) start_server();
}

void ftpsrv_disable(void) {
    if (phase == FTP_OFF) return;
    stop_server();
    phase = FTP_OFF;
    memset(password, 0, sizeof password);
    log_msg("ftp: disabled");
}

int ftpsrv_tick(int wifi_connected) {
    if (phase == FTP_WAIT_WIFI && wifi_connected) start_server();
    else if (phase == FTP_ON && !wifi_connected) {   // Wi-Fi lost (switch, router): off by itself
        log_msg("ftp: Wi-Fi lost, switching off");
        ftpsrv_disable();
        return 1;
    }
    return 0;
}

void ftpsrv_status(FtpStatus *out) {
    out->phase = phase;
    memcpy(out->password, password, sizeof out->password);
    snprintf(out->client_ip, sizeof out->client_ip, "%s", (const char *)client_ip);
    out->bytes = phase == FTP_ON ? mftp_bytes - bytes_at_start : 0;
}
