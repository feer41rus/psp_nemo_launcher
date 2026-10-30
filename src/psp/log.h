#ifndef NEMO_LOG_H
#define NEMO_LOG_H

#define NEMO_DATA_DIR "ms0:/PSP/NEMO"
#define NEMO_LOG_DIR  NEMO_DATA_DIR "/logs"   // 1.3.3: every log in one folder (author)
#define NEMO_LOG_PATH NEMO_LOG_DIR "/nemo.log"

// Start a new log for this run, keeping keep-1 previous runs as nemo.1.log ... (logrot.h).
// keep == 0: no log file at all this run, log_msg() does nothing.
void log_open(int keep);
// Settings › Debug › Keep logs = Off while running: close the log for the rest of this run.
void log_disable(void);
// Reopen the log in append mode (after sleep the old handle may be gone). No-op when disabled.
void log_reopen(void);
// USB mode: the PC owns the memory stick — close the log and drop messages until resumed.
void log_pause(void);
void log_resume(void);
// printf-style line prefixed with milliseconds since start; flushed immediately. Thread-safe.
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Detailed log (Settings › System › Debug, 1.3.3, off by default): log_dbg() lines only while it is on.
extern volatile int log_verbose;
void log_set_verbose(int on);
#define log_dbg(...) do { if (log_verbose) log_msg(__VA_ARGS__); } while (0)
// Something failed: always logged as "FAIL: ..."; with the detailed log off, a hint to turn it on is due
// (once a session; log_take_hint() hands it to the main thread).
void log_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  log_take_hint(void);

#endif
