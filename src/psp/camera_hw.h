#ifndef NEMO_CAMERA_HW_H
#define NEMO_CAMERA_HW_H

#include <stdint.h>
#include "camfilter.h"

// Go!Cam (PSP-450) through sceUsbCam (1.3.6). Connecting runs on its own thread (up to a few seconds);
// then a camera thread reads Motion JPEG frames 480x272 for the viewfinder (filtered) and, while recording,
// writes them into an AVI with the microphone's sound. Written without a camera: everything goes to the log.
typedef enum { CS_OFF, CS_CONNECTING, CS_READY, CS_NO_CAMERA, CS_FAILED } CamState;

void     cam_start(void);          // connect (async)
CamState cam_state(void);
void     cam_stop(void);           // stops a recording (file completed) and releases everything
int      cam_take_frame(const uint32_t **px, int *w, int *h);   // 1 = a new viewfinder picture (RGBA, packed)
void     cam_frame_done(void);
void     cam_set_filter(CamFilter f);
int      cam_zoom(int delta);      // the zoom after the step: 10 = 1x
int      cam_lens_toward_user(void);
float    cam_fps(void);
// A photo (640x480, filtered) into path, on the camera thread; the result comes from cam_take_photo_result.
void     cam_photo(const char *path);
int      cam_take_photo_result(int *rc);   // 1 = a photo finished (rc 0 = saved)
int      cam_record_start(const char *path);   // 0 started
int      cam_record_stop(void);    // 0 = a complete file
int      cam_recording(void);
uint32_t cam_record_ms(void);
int      cam_record_take_stopped(void);   // 1 once when the recording stopped by itself (card full, 2 GB)

#endif
