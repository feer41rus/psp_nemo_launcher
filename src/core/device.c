#include "device.h"
#include <stdio.h>

const char *device_model_name(int model) {
    switch (model) {
    case 0: return "PSP-1000";
    case 1: return "PSP-2000";
    case 2: case 3: case 6: case 8: return "PSP-3000";
    case 4: return "PSP Go";
    case 10: return "PSP-E1000";
    default: return "PSP";
    }
}

int device_has_wifi(int model) {
    return model != 10;
}

void device_gen_string(int model, char *buf, size_t cap) {
    snprintf(buf, cap, "%02dg", model + 1);
}

void device_fw_string(uint32_t devkit, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u%u", (unsigned)(devkit >> 24) & 0xFF, (unsigned)(devkit >> 16) & 0xFF,
             (unsigned)(devkit >> 8) & 0xFF);
}
