#ifndef NEMO_IDLE_H
#define NEMO_IDLE_H

#include <stdint.h>

// Screen-off timer for the USB / FTP screens: off after IDLE_OFF_MS without a press while active;
// the press that turns the screen back on does nothing else.
#define IDLE_OFF_MS 60000

typedef struct {
    uint32_t last_ms;   // last press, or when it became active
    int active;
    int off;
} Idle;

void idle_reset(Idle *d, uint32_t now_ms);    // the screen opened now
// Call every frame. Returns 1 while the screen should be off.
int idle_update(Idle *d, uint32_t now_ms, int active);
// A button was pressed. Returns 1 when the press only woke the screen (the caller drops it).
int idle_press(Idle *d, uint32_t now_ms);
// Milliseconds left before the screen goes off (0 when inactive or already due).
uint32_t idle_remaining_ms(const Idle *d, uint32_t now_ms);

#endif
