#ifndef NEMO_BATTERY_HW_H
#define NEMO_BATTERY_HW_H

// Battery readings (1.2.2): scePower* plus read-only syscon getters of the kernel (cycles, serial...).
// Nothing is ever written to the battery or the system controller. -1 = no data.
typedef struct {
    int exists, charging, pct, mv, temp, ma, life_min, rcap, fcap;
    int cycles, total_elec, charge_time;
    unsigned serial;
    int have_serial;
} BatInfo;

void battery_hw_init(void);                   // resolve the syscon getters once
void battery_hw_read(BatInfo *b, int full);   // full = 0: exists/charging/pct/mv/temp only

#endif
