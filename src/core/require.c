#include "require.h"
#include <stdio.h>

#define MIN_DEVKIT 0x06060000u
#define ARK_MAJOR 5

ReqResult require_check(uint32_t devkit, int ark_major, char *fw_out, size_t cap) {
    unsigned major = (devkit >> 24) & 0xF, minor = ((devkit >> 16) & 0xFF) * 10 + ((devkit >> 12) & 0xF);
    snprintf(fw_out, cap, "%u.%02u", major, (minor / 10 % 10) * 10 + minor % 10);
    if (devkit < MIN_DEVKIT) return REQ_OLD_FIRMWARE;
    return ark_major == ARK_MAJOR ? REQ_OK : REQ_NOT_ARK5;
}
