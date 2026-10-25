#include "queue.h"
#include <string.h>

#define PREV_RESTART_MS 3000

static uint32_t rnd(Queue *q) {   // xorshift32
    uint32_t x = q->rng ? q->rng : 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    q->rng = x;
    return x;
}

static void rebuild(Queue *q) {
    for (int i = 0; i < q->count; i++) q->items[i] = q->orig[q->ord[i]];
}

// Put original index `first` at position 0 and shuffle the rest after it.
static void shuffle_from(Queue *q, int first) {
    int n = 0;
    q->ord[n++] = first;
    for (int i = 0; i < q->count; i++) if (i != first) q->ord[n++] = i;
    for (int i = q->count - 1; i > 1; i--) {   // Fisher-Yates over positions 1..count-1
        int j = 1 + (int)(rnd(q) % (uint32_t)i);
        int t = q->ord[i];
        q->ord[i] = q->ord[j];
        q->ord[j] = t;
    }
    q->pos = 0;
}

void queue_init(Queue *q, uint32_t seed) {
    memset(q, 0, sizeof *q);
    q->rng = seed ? seed : 1;
}

void queue_set(Queue *q, const int *tracks, int n, int start) {
    if (n > QUEUE_MAX) n = QUEUE_MAX;
    if (n < 0) n = 0;
    q->count = n;
    for (int i = 0; i < n; i++) {
        q->orig[i] = tracks[i];
        q->ord[i] = i;
    }
    if (start < 0 || start >= n) start = 0;
    q->pos = start;
    if (q->shuffle && n > 0) shuffle_from(q, start);
    rebuild(q);
}

int queue_current(const Queue *q) {
    return (q->count > 0 && q->pos >= 0 && q->pos < q->count) ? q->items[q->pos] : -1;
}

int queue_append(Queue *q, int track) {
    if (q->count >= QUEUE_MAX) return -1;
    q->orig[q->count] = track;
    q->ord[q->count] = q->count;
    q->count++;
    rebuild(q);
    return 0;
}

int queue_remove(Queue *q, int index) {
    if (index < 0 || index >= q->count || index == q->pos) return -1;
    int o = q->ord[index];
    memmove(&q->orig[o], &q->orig[o + 1], (size_t)(q->count - o - 1) * sizeof q->orig[0]);
    memmove(&q->ord[index], &q->ord[index + 1], (size_t)(q->count - index - 1) * sizeof q->ord[0]);
    q->count--;
    for (int i = 0; i < q->count; i++) if (q->ord[i] > o) q->ord[i]--;
    if (index < q->pos) q->pos--;
    rebuild(q);
    return 0;
}

int queue_jump(Queue *q, int index) {
    if (index < 0 || index >= q->count) return -1;
    q->pos = index;
    return queue_current(q);
}

int queue_next(Queue *q, int by_user) {
    if (q->count == 0) return -1;
    if (!by_user && q->repeat == REPEAT_ONE) return queue_current(q);
    if (q->pos + 1 < q->count) {
        q->pos++;
        return queue_current(q);
    }
    if (q->repeat == REPEAT_ALL) {
        q->pos = 0;
        return queue_current(q);
    }
    return -1;
}

int queue_prev(Queue *q, uint32_t position_ms, int *restart) {
    if (q->count == 0) { *restart = 0; return -1; }
    if (position_ms > PREV_RESTART_MS || q->pos == 0) {
        *restart = 1;
        return queue_current(q);
    }
    *restart = 0;
    q->pos--;
    return queue_current(q);
}

int queue_at_last(const Queue *q) { return q->count == 0 || q->pos == q->count - 1; }

void queue_set_shuffle(Queue *q, int on) {
    on = on ? 1 : 0;
    if (q->shuffle == on) return;
    q->shuffle = on;
    if (q->count == 0) return;
    int cur = q->ord[q->pos];
    if (on) {
        shuffle_from(q, cur);
    } else {
        for (int i = 0; i < q->count; i++) q->ord[i] = i;
        q->pos = cur;
    }
    rebuild(q);
}

void queue_cycle_repeat(Queue *q) {
    q->repeat = q->repeat == REPEAT_OFF ? REPEAT_ALL : q->repeat == REPEAT_ALL ? REPEAT_ONE : REPEAT_OFF;
}
