#ifndef NEMO_REQUIRE_H
#define NEMO_REQUIRE_H

#include <stddef.h>
#include <stdint.h>

// Nemo runs on firmware 6.60+ with ARK-5 (author, 1.1.8).
typedef enum { REQ_OK, REQ_OLD_FIRMWARE, REQ_NOT_ARK5 } ReqResult;

// devkit: sceKernelDevkitVersion() (0x06061010 = 6.61); ark_major: sctrlSEGetVersion() (5 = ARK-5).
// fw_out gets the firmware as "6.61".
ReqResult require_check(uint32_t devkit, int ark_major, char *fw_out, size_t cap);

#endif
