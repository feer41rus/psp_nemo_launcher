	.set noreorder
#include "pspimport.s"

// Every import of these libraries lives here, in one object. psp-fixup-imports assumes a library's
// stubs are contiguous; when PSPSDK split stubs come from several places (Nemo, libcglue, SDL,
// libpspftp) the counts spill into the next library and calls get unlinked at module load
// (sceNetInit -> 0x8002013A, see docs/research/2026-10-25-network-spike.md). tools/check_stubs.py
// verifies the result after every build. NIDs: PSPSDK stubs; scan functions: PPSSPP sceNet.cpp.

IMPORT_START "sceNet",0x00090000
IMPORT_FUNC  "sceNet",0x39AF39A6,sceNetInit
IMPORT_FUNC  "sceNet",0x281928A9,sceNetTerm

IMPORT_START "sceNetInet",0x00090000
IMPORT_FUNC  "sceNetInet",0x17943399,sceNetInetInit
IMPORT_FUNC  "sceNetInet",0xA9ED66B9,sceNetInetTerm
IMPORT_FUNC  "sceNetInet",0xDB094E1B,sceNetInetAccept
IMPORT_FUNC  "sceNetInet",0x1A33F9AE,sceNetInetBind
IMPORT_FUNC  "sceNetInet",0x8D7284EA,sceNetInetClose
IMPORT_FUNC  "sceNetInet",0x410B34AA,sceNetInetConnect
IMPORT_FUNC  "sceNetInet",0xFBABE411,sceNetInetGetErrno
IMPORT_FUNC  "sceNetInet",0xD10A1A7A,sceNetInetListen
IMPORT_FUNC  "sceNetInet",0xCDA85C99,sceNetInetRecv
IMPORT_FUNC  "sceNetInet",0x7AA671BC,sceNetInetSend
IMPORT_FUNC  "sceNetInet",0x2FE71FE7,sceNetInetSetsockopt
IMPORT_FUNC  "sceNetInet",0x8B7B220F,sceNetInetSocket

IMPORT_START "sceNetResolver",0x00090000
IMPORT_FUNC  "sceNetResolver",0xF3370E61,sceNetResolverInit
IMPORT_FUNC  "sceNetResolver",0x6138194A,sceNetResolverTerm

IMPORT_START "sceNetApctl",0x00090000
IMPORT_FUNC  "sceNetApctl",0xE2F91F9B,sceNetApctlInit
IMPORT_FUNC  "sceNetApctl",0xB3EDD0EC,sceNetApctlTerm
IMPORT_FUNC  "sceNetApctl",0x2BEFDF23,sceNetApctlGetInfo
IMPORT_FUNC  "sceNetApctl",0x8ABADD51,sceNetApctlAddHandler
IMPORT_FUNC  "sceNetApctl",0x5963991B,sceNetApctlDelHandler
IMPORT_FUNC  "sceNetApctl",0xCFB957C6,sceNetApctlConnect
IMPORT_FUNC  "sceNetApctl",0x24FE91A1,sceNetApctlDisconnect
IMPORT_FUNC  "sceNetApctl",0x5DEAC81B,sceNetApctlGetState
IMPORT_FUNC  "sceNetApctl",0xE9B2E5E6,sceNetApctlScanUser
IMPORT_FUNC  "sceNetApctl",0x6BDDCB8C,sceNetApctlGetBSSDescIDListUser
IMPORT_FUNC  "sceNetApctl",0x04776994,sceNetApctlGetBSSDescEntryUser

IMPORT_START "sceUtility",0x40090000
IMPORT_FUNC  "sceUtility",0x2A2B3DE0,sceUtilityLoadModule
IMPORT_FUNC  "sceUtility",0xE49BFE92,sceUtilityUnloadModule
IMPORT_FUNC  "sceUtility",0x5EEE6548,sceUtilityCheckNetParam
IMPORT_FUNC  "sceUtility",0x434D4B3A,sceUtilityGetNetParam
IMPORT_FUNC  "sceUtility",0xA5DA2406,sceUtilityGetSystemParamInt
IMPORT_FUNC  "sceUtility",0x9A1C91D7,sceUtilityMsgDialogGetStatus
IMPORT_FUNC  "sceUtility",0x2AD8E239,sceUtilityMsgDialogInitStart
IMPORT_FUNC  "sceUtility",0x67AF3428,sceUtilityMsgDialogShutdownStart
IMPORT_FUNC  "sceUtility",0x95FC253B,sceUtilityMsgDialogUpdate
IMPORT_FUNC  "sceUtility",0xF3F76017,sceUtilityOskGetStatus
IMPORT_FUNC  "sceUtility",0xF6269B82,sceUtilityOskInitStart
IMPORT_FUNC  "sceUtility",0x3DFAEBA9,sceUtilityOskShutdownStart
IMPORT_FUNC  "sceUtility",0x4B85C861,sceUtilityOskUpdate

IMPORT_START "sceUtility_netparam_internal",0x40090000
IMPORT_FUNC  "sceUtility_netparam_internal",0x072DEBF2,sceUtilityCreateNetParam
IMPORT_FUNC  "sceUtility_netparam_internal",0x9CE50172,sceUtilityDeleteNetParam
IMPORT_FUNC  "sceUtility_netparam_internal",0xFB0C4840,sceUtilityCopyNetParam
IMPORT_FUNC  "sceUtility_netparam_internal",0xFC4516F3,sceUtilitySetNetParam

IMPORT_START "sceUsb",0x40090000
IMPORT_FUNC  "sceUsb",0xAE5DE6AF,sceUsbStart
IMPORT_FUNC  "sceUsb",0xC2464FA0,sceUsbStop
IMPORT_FUNC  "sceUsb",0x586DB82C,sceUsbActivate
IMPORT_FUNC  "sceUsb",0xC572A9C8,sceUsbDeactivate
IMPORT_FUNC  "sceUsb",0xC21645A4,sceUsbGetState
