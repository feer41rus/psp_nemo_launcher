#include "usb.h"
#include "log.h"
#include <kubridge.h>
#include <pspkernel.h>
#include <pspusb.h>
#include <pspusbstor.h>

// Reference: Launcher-Menus/arkMenu/src/usb.cpp (same modules, drivers and activation PID).
#define USBSTOR_EF_DRIVERNAME "USBStorEFlash_Driver"
#define USB_PID 0x1c8

static int modules_loaded, active;

static void load_modules(void) {
    static const char *const mods[] = {
        "flash0:/kd/semawm.prx",   "flash0:/kd/usbstor.prx",       "flash0:/kd/usbstormgr.prx",
        "flash0:/kd/usbstorms.prx", "flash0:/kd/usbstoreflash.prx", "flash0:/kd/usbstorboot.prx",
    };
    for (unsigned i = 0; i < sizeof mods / sizeof *mods; i++) {
        SceUID id = kuKernelLoadModule(mods[i], 0, NULL);
        int rc = id >= 0 ? sceKernelStartModule(id, 0, NULL, NULL, NULL) : id;
        log_msg("usb: %s load=%08X start=%08X", mods[i], (unsigned)id, (unsigned)rc);
    }
    modules_loaded = 1;
}

int usb_enter(void) {
    if (active) return 0;
    if (!modules_loaded) load_modules();
    int a = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    int b = sceUsbStart(PSP_USBSTOR_DRIVERNAME, 0, 0);
    int c = sceUsbStart(USBSTOR_EF_DRIVERNAME, 0, 0);
    int d = sceUsbActivate(USB_PID);
    log_msg("usb: start bus=%08X stor=%08X ef=%08X activate=%08X", (unsigned)a, (unsigned)b, (unsigned)c,
            (unsigned)d);
    if (a < 0 || b < 0 || d < 0) {
        usb_leave();
        return -1;
    }
    active = 1;
    return 0;
}

void usb_leave(void) {
    sceUsbDeactivate(USB_PID);
    sceUsbStop(USBSTOR_EF_DRIVERNAME, 0, 0);
    sceUsbStop(PSP_USBSTOR_DRIVERNAME, 0, 0);
    sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
    active = 0;
}

int usb_active(void) { return active; }
int usb_cable(void) { return (sceUsbGetState() & PSP_USB_CABLE_CONNECTED) != 0; }
int usb_connected(void) { return (sceUsbGetState() & PSP_USB_CONNECTION_ESTABLISHED) != 0; }
