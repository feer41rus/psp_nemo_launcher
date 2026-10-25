#ifndef NEMO_USB_H
#define NEMO_USB_H

// USB mass storage (the memory stick on a PC), as ARK's own launcher does it. While it is on,
// Nemo must not touch ms0: (the log is paused by the caller).
int  usb_enter(void);       // 0 ok
void usb_leave(void);
int  usb_active(void);
int  usb_cable(void);       // cable plugged into a PC
int  usb_connected(void);   // the PC mounted the stick

#endif
