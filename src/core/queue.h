#ifndef NEMO_QUEUE_H
#define NEMO_QUEUE_H

#include <stdint.h>

typedef enum { REPEAT_OFF, REPEAT_ALL, REPEAT_ONE } RepeatMode;

#define QUEUE_MAX 1024

// Play queue of track indices. items[] is the play order shown to the user; when shuffled it is a
// permutation (ord) of the original order (orig), so switching shuffle off restores the order.
typedef struct {
    int items[QUEUE_MAX];
    int count, pos;
    int shuffle;
    RepeatMode repeat;
    uint32_t rng;
    int orig[QUEUE_MAX];   // tracks in their original order
    int ord[QUEUE_MAX];    // items[i] == orig[ord[i]]
} Queue;

void queue_init(Queue *q, uint32_t seed);
void queue_set(Queue *q, const int *tracks, int n, int start);   // shuffle: start track first, rest shuffled
int  queue_current(const Queue *q);                              // -1 if empty
int  queue_append(Queue *q, int track);                          // to the end; -1 when full
int  queue_remove(Queue *q, int index);                          // -1 for the current track / bad index
int  queue_jump(Queue *q, int index);                            // new current or -1
int  queue_next(Queue *q, int by_user);                          // new current, -1 at the end
int  queue_prev(Queue *q, uint32_t position_ms, int *restart);   // restart = 1: replay current from 0
int  queue_at_last(const Queue *q);                              // ignores repeat ("until end of list")
void queue_set_shuffle(Queue *q, int on);
void queue_cycle_repeat(Queue *q);

#endif
