#ifndef NEMO_UMD_H
#define NEMO_UMD_H

// The UMD drive (1.2.5): a low-priority thread watches it; the main loop takes the events.
typedef enum { UMD_EV_NONE, UMD_EV_GAME, UMD_EV_VIDEO, UMD_EV_MUSIC, UMD_EV_OTHER, UMD_EV_REMOVED } UmdEventKind;

#define UMD_MAX_STREAMS 64

typedef struct {
    UmdEventKind kind;
    char title[128];
    char disc_id[16];
    int at_start;   // the disc was already in when Nemo started (no notice)
    char streams[UMD_MAX_STREAMS][24];   // UMD Music (1.3.5): UMD_AUDIO/STREAM/*.MPS
    int n_streams;
} UmdEvent;

void umd_start(void);              // not on a PSP Go (no drive)
int  umd_take_event(UmdEvent *e);  // 1 = an event (main thread)
int  umd_current(UmdEvent *e);     // 1 = a game disc is in now (to put it back after a rescan)

#endif
