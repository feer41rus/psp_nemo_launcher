#ifndef NEMO_BATTERY_H
#define NEMO_BATTERY_H

#include <stddef.h>

// Battery screens and the capacity test (1.2.2): design capacity, wear, value formatting and the CSV log
// the test writes every 10 s (ms0:/PSP/NEMO/battery/YYYYMMDD-HHMMSS.csv).

int  battery_design_mah(int model);                    // rated capacity: PSP-1000 1800, Go 930, others 1200
int  battery_wear_pct(int full_mah, int design_mah);   // 0..100, -1 unknown
int  battery_unreliable(int model);                    // Go / E1000: no full battery controller
void battery_fmt_volts(char *out, size_t cap, int mv, int lang_en);          // "4,02 В"; mv <= 0 -> "—"
void battery_fmt_minutes(char *out, size_t cap, int minutes, int lang_en);   // "3 ч 12 мин"; < 0 -> "—"

typedef enum { BT_STANDARD, BT_LIGHT } BatTestMode;
// sec: since the start; ram: free KB (v2). -1 = no data.
typedef struct { int sec, pct, mv, temp, ma, cpu, fps, kbs, ram; } BatSample;

// Log format: "# nemo-battery v2,mode=...,model=...,start=..." then one sample per line, and
// "# end,aborted=0|1" when the test stopped in Nemo (no end line = the PSP switched itself off).
// v1 logs (1.2.2 first build: minutes, no ram column) are still read.
int  batlog_header(char *out, size_t cap, BatTestMode mode, const char *model, const char *start_iso);
int  batlog_line(char *out, size_t cap, const BatSample *s);
int  batlog_footer(char *out, size_t cap, int aborted);

typedef struct {
    BatTestMode mode;
    char start[20];
    int minutes, pct_start, pct_end, mv_start, mv_end, temp_max;
    int finished, aborted, samples;
} BatSummary;

// Summary of a log; up to max_pts samples with data (decimated, first and last kept) go to pts.
// A cut-off last line is ignored. 0 ok, -1 not a Nemo battery log.
int  batlog_parse(const char *text, BatSummary *s, BatSample *pts, int max_pts, int *n_pts);
// A log without an end line whose battery was not empty yet: the PSP switched off for another reason.
#define BAT_EMPTY_PCT 10
int  batlog_cut_short(const BatSummary *s);
// Indices of "YYYYMMDD-HHMM[SS].csv" names to delete so the `keep` newest remain; other names are ignored.
int  batlog_prune(const char *const *names, int n, int keep, int *to_delete);

// Power settings the test switches off (auto sleep, backlight auto-off) and the brightness it forces,
// kept in a file until they are put back: at the end of the test or at the next Nemo start. -1 = unknown.
typedef struct { int suspend_s, backlight_s, bright; } BatGuard;
int  batguard_format(char *out, size_t cap, const BatGuard *g);
int  batguard_parse(const char *text, BatGuard *g);   // 0 ok, -1 a key is missing

#endif
