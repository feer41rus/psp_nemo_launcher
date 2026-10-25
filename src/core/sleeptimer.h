#ifndef NEMO_SLEEPTIMER_H
#define NEMO_SLEEPTIMER_H

#include <stdint.h>

typedef enum { SLEEP_OFF, SLEEP_15, SLEEP_30, SLEEP_45, SLEEP_60, SLEEP_END_OF_LIST, SLEEP_COUNT } SleepChoice;

typedef struct {
    SleepChoice choice;
    uint32_t deadline_ms;     // timed choices only
} SleepTimer;

void sleep_set(SleepTimer *t, SleepChoice c, uint32_t now_ms);
SleepChoice sleep_step(SleepChoice c, int dir);              // wraps around
int sleep_fade_percent(const SleepTimer *t, uint32_t now_ms); // 100, linear to 0 over the last 60 s
int sleep_due(const SleepTimer *t, uint32_t now_ms);          // timed choices: deadline reached
uint32_t sleep_remaining_ms(const SleepTimer *t, uint32_t now_ms);

#endif
