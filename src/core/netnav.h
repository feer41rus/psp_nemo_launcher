#ifndef NEMO_NETNAV_H
#define NEMO_NETNAV_H

#include "input.h"

// Network tab (layout F): section column on the left, panel of the section on the right.
typedef enum { NS_WIFI, NS_FTP, NS_USB, NS_WPA2, NS_USBNET } NetSection;
typedef enum { NF_COLUMN, NF_PANEL } NetFocus;
typedef enum {
    NC_NONE, NC_ROW, NC_REFRESH, NC_FORGET, NC_FTP_TOGGLE, NC_USB_OPEN, NC_WPA2_TOGGLE, NC_BACK_TO_TAB
} NetCmdType;

typedef struct {
    NetCmdType type;
    int index;             // NC_ROW / NC_FORGET: Wi-Fi panel row
} NetCmd;

typedef struct {
    int has_wifi;
    NetSection sections[5];
    int section_count;
    int section;           // index into sections
    NetFocus focus;
    int row;               // Wi-Fi panel row
} NetNav;

typedef struct {
    int rows;              // rows of the Wi-Fi panel list
} NetNavModel;

// With Wi-Fi: WIFI, FTP, USB (WPA2 is in Settings › System). Without (E1000): USB, USBNET (shown disabled, does nothing).
void netnav_init(NetNav *n, int has_wifi);
NetSection netnav_current(const NetNav *n);
NetCmd netnav_handle(NetNav *n, Action a, const NetNavModel *m);

#endif
