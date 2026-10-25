#ifndef NEMO_LOG_H
#define NEMO_LOG_H

#define NEMO_LOG_PATH      "ms0:/PSP/NEMO/nemo.log"
#define NEMO_LOG_PREV_PATH "ms0:/PSP/NEMO/nemo.prev.log"

// Start a new log for this run (the previous run is kept as nemo.prev.log).
void log_open(void);
// Reopen the log in append mode (after sleep the old handle may be gone).
void log_reopen(void);
// USB mode: the PC owns the memory stick — close the log and drop messages until resumed.
void log_pause(void);
void log_resume(void);
// printf-style line prefixed with milliseconds since start; flushed immediately. Thread-safe.
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
