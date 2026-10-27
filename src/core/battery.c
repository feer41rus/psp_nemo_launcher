#include "battery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int battery_design_mah(int model) {
    if (model == 0) return 1800;
    if (model == 4) return 930;
    return 1200;
}

int battery_wear_pct(int full_mah, int design_mah) {
    if (full_mah <= 0 || design_mah <= 0) return -1;
    if (full_mah >= design_mah) return 0;
    return (design_mah - full_mah) * 100 / design_mah;
}

int battery_unreliable(int model) { return model == 4 || model == 10; }

void battery_fmt_volts(char *out, size_t cap, int mv, int lang_en) {
    if (mv <= 0) { snprintf(out, cap, "—"); return; }
    int cv = (mv + 5) / 10;   // hundredths of a volt
    snprintf(out, cap, lang_en ? "%d.%02d V" : "%d,%02d В", cv / 100, cv % 100);
}

void battery_fmt_minutes(char *out, size_t cap, int minutes, int lang_en) {
    if (minutes < 0) { snprintf(out, cap, "—"); return; }
    if (minutes < 60) snprintf(out, cap, lang_en ? "%d min" : "%d мин", minutes);
    else snprintf(out, cap, lang_en ? "%d h %d min" : "%d ч %d мин", minutes / 60, minutes % 60);
}

int batlog_header(char *out, size_t cap, BatTestMode mode, const char *model, const char *start_iso) {
    return snprintf(out, cap, "# nemo-battery v2,mode=%s,model=%s,start=%s\n",
                    mode == BT_LIGHT ? "light" : "standard", model, start_iso);
}

int batlog_line(char *out, size_t cap, const BatSample *s) {
    return snprintf(out, cap, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", s->sec, s->pct, s->mv, s->temp, s->ma, s->cpu,
                    s->fps, s->kbs, s->ram);
}

int batlog_footer(char *out, size_t cap, int aborted) {
    return snprintf(out, cap, "# end,aborted=%d\n", aborted ? 1 : 0);
}

// One complete sample line (ends with '\n'; v1: 8 fields in minutes, v2: 9 in seconds); 0 otherwise.
static int read_sample(const char *line, const char *eol, int v1, BatSample *s) {
    if (!eol || *line == '#') return 0;
    int v[9], n = 0, want = v1 ? 8 : 9;
    const char *p = line;
    while (p < eol && n < want) {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p) return 0;
        v[n++] = (int)x;
        p = end;
        if (p < eol && *p == ',') p++;
        else break;
    }
    if (n != want) return 0;
    BatSample r = { v1 ? v[0] * 60 : v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v1 ? -1 : v[8] };
    *s = r;
    return 1;
}

static int has_data(const BatSample *s) { return s->pct >= 0 && s->mv > 0; }

int batlog_parse(const char *text, BatSummary *s, BatSample *pts, int max_pts, int *n_pts) {
    memset(s, 0, sizeof *s);
    s->pct_start = s->pct_end = s->mv_start = s->mv_end = s->temp_max = -1;
    *n_pts = 0;
    int v1 = strncmp(text, "# nemo-battery v1", 17) == 0;
    if (!v1 && strncmp(text, "# nemo-battery v2", 17) != 0) return -1;
    const char *nl = strchr(text, '\n');
    char head[160];
    snprintf(head, sizeof head, "%.*s", nl ? (int)(nl - text) : 150, text);
    s->mode = strstr(head, "mode=light") ? BT_LIGHT : BT_STANDARD;
    const char *st = strstr(head, "start=");
    if (st) snprintf(s->start, sizeof s->start, "%.16s", st + 6);
    s->finished = 1;
    int valid = 0;   // pass 1: summary and the number of samples with data
    for (const char *line = nl ? nl + 1 : text + strlen(text); *line;) {
        const char *eol = strchr(line, '\n');
        BatSample b;
        if (strncmp(line, "# end", 5) == 0) s->aborted = strncmp(line, "# end,aborted=1", 15) == 0;
        if (read_sample(line, eol, v1, &b)) {
            s->samples++;
            s->minutes = b.sec / 60;
            if (has_data(&b)) {
                if (valid++ == 0) { s->pct_start = b.pct; s->mv_start = b.mv; }
                s->pct_end = b.pct;
                s->mv_end = b.mv;
                if (b.temp > s->temp_max) s->temp_max = b.temp;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    if (!pts || max_pts <= 0 || valid == 0) return 0;
    int step = (valid + max_pts - 1) / max_pts, k = 0, n = 0;   // pass 2: every step-th sample + the last
    BatSample last = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (const char *line = nl + 1; *line;) {
        const char *eol = strchr(line, '\n');
        BatSample b;
        if (read_sample(line, eol, v1, &b) && has_data(&b)) {
            if (k % step == 0 && n < max_pts) pts[n++] = b;
            last = b;
            k++;
        }
        if (!eol) break;
        line = eol + 1;
    }
    if (pts[n - 1].sec != last.sec) {
        if (n < max_pts) pts[n++] = last;
        else pts[n - 1] = last;
    }
    *n_pts = n;
    return 0;
}

int batlog_cut_short(const BatSummary *s) { return s->pct_end > BAT_EMPTY_PCT; }

static int is_log_name(const char *s) {
    size_t len = strlen(s);   // YYYYMMDD-HHMM.csv or YYYYMMDD-HHMMSS.csv
    if ((len != 17 && len != 19) || strcmp(s + len - 4, ".csv") != 0 || s[8] != '-') return 0;
    for (size_t i = 0; i < len - 4; i++)
        if (i != 8 && (s[i] < '0' || s[i] > '9')) return 0;
    return 1;
}

int batlog_prune(const char *const *names, int n, int keep, int *to_delete) {
    int idx[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (is_log_name(names[i])) idx[m++] = i;
    for (int i = 1; i < m; i++)   // oldest first (the name is the start time)
        for (int j = i; j > 0 && strcmp(names[idx[j - 1]], names[idx[j]]) > 0; j--) {
            int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    int del = m > keep ? m - keep : 0;
    for (int i = 0; i < del; i++) to_delete[i] = idx[i];
    return del;
}

int batguard_format(char *out, size_t cap, const BatGuard *g) {
    return snprintf(out, cap, "suspend=%d\nbacklight=%d\nbright=%d\n", g->suspend_s, g->backlight_s, g->bright);
}

static int guard_key(const char *text, const char *key, int *out) {
    const char *p = strstr(text, key);
    if (!p) return -1;
    char *end;
    long v = strtol(p + strlen(key), &end, 10);
    if (end == p + strlen(key)) return -1;
    *out = (int)v;
    return 0;
}

int batguard_parse(const char *text, BatGuard *g) {
    BatGuard r;
    if (guard_key(text, "suspend=", &r.suspend_s) || guard_key(text, "backlight=", &r.backlight_s) ||
        guard_key(text, "bright=", &r.bright))
        return -1;
    *g = r;
    return 0;
}
