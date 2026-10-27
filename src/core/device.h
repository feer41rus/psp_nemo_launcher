#ifndef NEMO_DEVICE_H
#define NEMO_DEVICE_H

#include <stddef.h>
#include <stdint.h>

// `model` is sceKernelGetModel(): 0 = 01g (PSP-1000), 1 = 02g (2000), 2/3/6/8 = 03g/04g/07g/09g (3000),
// 4 = 05g (Go), 10 = 11g (E1000 Street). Unknown values get the generic name "PSP".
const char *device_model_name(int model);
// Street (E1000) has no WLAN hardware; every other model does.
int device_has_wifi(int model);
// Motherboard generation label, e.g. "03g".
void device_gen_string(int model, char *buf, size_t cap);
// sceKernelDevkitVersion() 0xMMmmPP10 -> "M.mP", e.g. 0x06060110 -> "6.61".
void device_fw_string(uint32_t devkit, char *buf, size_t cap);
// Battery percent from the firmware: 0..100 is taken; anything else (an error code such as 0x802B0100)
// keeps prev (-1 = no data yet).
int battery_sanitize(int raw, int prev);

#endif
