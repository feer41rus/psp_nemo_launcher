#include "rtc.h"
#include "log.h"
#include <kubridge.h>
#include <psprtc.h>
#include <string.h>
#include <systemctrl.h>

int rtc_get_local(DateTime *out) {
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) return -1;
    out->year = t.year;
    out->month = t.month;
    out->day = t.day;
    out->hour = t.hour;
    out->minute = t.minute;
    return 0;
}

int rtc_set_local(const DateTime *local, int tz_minutes) {
    DateTime u;
    datetime_local_to_utc(local, tz_minutes, &u);
    ScePspDateTime t;
    memset(&t, 0, sizeof t);
    t.year = (unsigned short)u.year;
    t.month = (unsigned short)u.month;
    t.day = (unsigned short)u.day;
    t.hour = (unsigned short)u.hour;
    t.minute = (unsigned short)u.minute;
    static u64 tick;                          // passed to the kernel by address
    if (sceRtcGetTick(&t, &tick) < 0) return -1;
    // sceRtcSetCurrentTick is kernel-only. NIDs from ARK-5 SystemControl/src/nid_660_data.c.
    void *f = (void *)sctrlHENFindFunction("sceRTC_Service", "sceRtc_driver", 0x929620CE);
    if (!f) f = (void *)sctrlHENFindFunction("sceRTC_Service", "sceRtc_driver", 0x9763C138);
    if (!f) {
        log_msg("rtc: sceRtcSetCurrentTick not found");
        return -2;
    }
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    args.arg1 = (u32)&tick;
    int rc = kuKernelCall(f, &args);
    int ret = rc < 0 ? rc : (int)args.ret1;
    log_msg("rtc: set %04d-%02d-%02d %02d:%02d UTC (tz %+d min) rc=%08X", u.year, u.month, u.day, u.hour, u.minute,
            tz_minutes, (unsigned)ret);
    return ret < 0 ? ret : 0;
}
