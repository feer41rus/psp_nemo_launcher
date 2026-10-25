#include "idle.h"

void idle_reset(Idle *d, uint32_t now_ms) {
    d->last_ms = now_ms;
    d->active = 1;   // "the screen opened now": the minute counts from here
    d->off = 0;
}

int idle_update(Idle *d, uint32_t now_ms, int active) {
    if (!active) {
        d->active = 0;
        d->off = 0;
        return 0;
    }
    if (!d->active) {                 // a fresh minute when the USB / FTP screen opens
        d->active = 1;
        d->last_ms = now_ms;
    }
    if (!d->off && now_ms - d->last_ms >= IDLE_OFF_MS) d->off = 1;
    return d->off;
}

int idle_press(Idle *d, uint32_t now_ms) {
    d->last_ms = now_ms;
    if (!d->off) return 0;
    d->off = 0;
    return 1;
}

uint32_t idle_remaining_ms(const Idle *d, uint32_t now_ms) {
    if (!d->active || d->off) return 0;
    uint32_t spent = now_ms - d->last_ms;
    return spent >= IDLE_OFF_MS ? 0 : IDLE_OFF_MS - spent;
}
