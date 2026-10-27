#include "battery_hw.h"
#include "device.h"
#include "log.h"
#include <kubridge.h>
#include <psppower.h>
#include <systemctrl.h>
#include <string.h>

// Read-only getters of sceSyscon_driver; NIDs (6.60 value first, then the old one) from ARK-5
// SystemControl/src/nid_660_data.c.
static void *get_cycle, *get_serial, *get_total_elec, *get_charge_time;

static void *find_syscon(u32 new_nid, u32 old_nid) {
    void *f = (void *)sctrlHENFindFunction("sceSYSCON_Driver", "sceSyscon_driver", new_nid);
    if (!f) f = (void *)sctrlHENFindFunction("sceSYSCON_Driver", "sceSyscon_driver", old_nid);
    return f;
}

void battery_hw_init(void) {
    get_cycle = find_syscon(0x68AF19F1, 0xB5105D51);
    get_serial = find_syscon(0x031CCDD7, 0xD5340103);
    get_total_elec = find_syscon(0xF8F6E1F4, 0x4C539345);
    get_charge_time = find_syscon(0xF87A1D11, 0x4C0EE2FA);
    log_msg("battery: syscon cycle=%p serial=%p total_elec=%p charge_time=%p", get_cycle, get_serial,
            get_total_elec, get_charge_time);
}

// int f(int *out) in kernel mode; the value or -1.
static int kget(void *f, int *ok) {
    *ok = 0;
    if (!f) return -1;
    static int value;   // kernel writes it; static keeps it out of the caller's stack frame
    value = 0;
    struct KernelCallArg args;
    memset(&args, 0, sizeof args);
    args.arg1 = (u32)&value;
    if (kuKernelCall(f, &args) < 0 || (int)args.ret1 < 0) return -1;
    *ok = 1;
    return value;
}

static int nonneg(int v) { return v < 0 ? -1 : v; }

void battery_hw_read(BatInfo *b, int full) {
    memset(b, 0xff, sizeof *b);   // every field -1
    b->exists = scePowerIsBatteryExist() == 1;
    b->charging = scePowerIsBatteryCharging() == 1;
    if (!b->exists) return;
    b->pct = battery_sanitize(scePowerGetBatteryLifePercent(), -1);
    b->mv = nonneg(scePowerGetBatteryVolt());
    b->temp = nonneg(scePowerGetBatteryTemp());
    b->have_serial = 0;
    if (!full) return;
    int e = scePowerGetBatteryElec();
    b->ma = e > -100000 && e < 100000 ? e : -1;   // signed: negative = discharging
    b->life_min = b->charging ? -1 : nonneg(scePowerGetBatteryLifeTime());
    b->rcap = nonneg(scePowerGetBatteryRemainCapacity());
    b->fcap = nonneg(scePowerGetBatteryFullCapacity());
    int ok;
    b->cycles = kget(get_cycle, &ok);
    int s = kget(get_serial, &ok);
    b->have_serial = ok && s != 0 && s != -1;
    b->serial = (unsigned)s;
    b->total_elec = kget(get_total_elec, &ok);
    b->charge_time = kget(get_charge_time, &ok);
}
