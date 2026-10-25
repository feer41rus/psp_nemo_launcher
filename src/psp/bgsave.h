#ifndef NEMO_BGSAVE_H
#define NEMO_BGSAVE_H

// One file written on a worker thread, so the main loop never waits on the memory stick
// (track switch hitch: the music state save took 100-400 ms). The newest text always wins:
// an older pending or in-flight request never overwrites a newer write.
void bgsave_init(void);
// Queue a write; takes ownership of the malloc'ed text.
void bgsave_queue(const char *path, char *text);
// Write now on the calling thread (sleep, game launch, power off); drops any pending request.
int  bgsave_now(const char *path, char *text);
// Drop a pending request; one already writing is superseded and skipped (Settings › Reset).
void bgsave_cancel(void);

#endif
