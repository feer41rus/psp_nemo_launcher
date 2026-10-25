#include "arksettings.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "safefile.h"

#define SETTINGS_MAX (256 * 1024)

// Compare the n-th comma-separated field of [line, end) (trimmed) with want, ignoring case.
static int field_is(const char *line, const char *end, int n, const char *want) {
    const char *p = line;
    for (int i = 0; i < n; i++) {
        p = memchr(p, ',', (size_t)(end - p));
        if (!p) return 0;
        p++;
    }
    const char *q = memchr(p, ',', (size_t)(end - p));
    if (!q) q = end;
    while (p < q && (*p == ' ' || *p == '\t')) p++;
    while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r')) q--;
    size_t len = strlen(want);
    return (size_t)(q - p) == len && strncasecmp(p, want, len) == 0;
}

int arksettings_get(const char *text, const char *key, int default_on) {
    int value = default_on;
    const char *line = text;
    while (*line) {   // ARK applies lines in order, so the last matching one wins
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);
        if (field_is(line, end, 0, "always") && field_is(line, end, 1, key)) {
            if (field_is(line, end, 2, "on")) value = 1;
            else if (field_is(line, end, 2, "off")) value = 0;
        }
        if (!nl) break;
        line = nl + 1;
    }
    return value;
}

char *arksettings_set_launcher(const char *text, int on) { return arksettings_set(text, "launcher", on); }

char *arksettings_set(const char *text, const char *key, int on) {
    char value[96];
    snprintf(value, sizeof value, "always, %s, %s", key, on ? "on" : "off");
    size_t vlen = strlen(value), tlen = strlen(text);
    const char *eol = strstr(text, "\r\n") ? "\r\n" : "\n";
    // Worst case: every line replaced (each grows by at most vlen) plus one appended line.
    size_t cap = tlen + vlen + 4;
    for (const char *p = text; *p; p++) if (*p == '\n') cap += vlen;
    char *out = malloc(cap + 1);
    if (!out) return NULL;

    size_t o = 0;
    int replaced = 0;
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        const char *end = nl ? nl : line + strlen(line);
        const char *content_end = (end > line && end[-1] == '\r') ? end - 1 : end;
        if (field_is(line, content_end, 0, "always") && field_is(line, content_end, 1, key)) {
            memcpy(out + o, value, vlen);
            o += vlen;
            memcpy(out + o, content_end, (size_t)(end - content_end));   // keep "\r" if present
            o += (size_t)(end - content_end);
            replaced = 1;
        } else {
            memcpy(out + o, line, (size_t)(end - line));
            o += (size_t)(end - line);
        }
        if (!nl) break;
        out[o++] = '\n';
        line = nl + 1;
    }
    if (!replaced) {
        if (o > 0 && out[o - 1] != '\n') { memcpy(out + o, eol, strlen(eol)); o += strlen(eol); }
        memcpy(out + o, value, vlen);
        o += vlen;
        memcpy(out + o, eol, strlen(eol));
        o += strlen(eol);
    }
    out[o] = '\0';
    return out;
}

int arksettings_update_file(const char *path, const char *fallback_path, int on) {
    return arksettings_update_key_file(path, fallback_path, "launcher", on, 0);
}

int arksettings_read_key_file(const char *path, const char *fallback_path, const char *key, int default_on) {
    int missing = 0;
    char *text = safe_read_text(path, SETTINGS_MAX, &missing);
    if (!text && fallback_path) text = safe_read_text(fallback_path, SETTINGS_MAX, &missing);
    if (!text) return default_on;
    int v = arksettings_get(text, key, default_on);
    free(text);
    return v;
}

int arksettings_update_key_file(const char *path, const char *fallback_path, const char *key, int on, int default_on) {
    int missing = 0;
    char *text = safe_read_text(path, SETTINGS_MAX, &missing);
    if (!text && !missing) return -1;                 // exists but unreadable: never overwrite
    if (!text) {
        if (on == default_on) return 0;               // ARK's default already gives this value
        int fb_missing;
        text = fallback_path ? safe_read_text(fallback_path, SETTINGS_MAX, &fb_missing) : NULL;
        if (!text) text = calloc(1, 1);
        if (!text) return -1;
    }
    char *next = arksettings_set(text, key, on);
    int rc = 0;
    if (!next) rc = -1;
    else if (!missing && strcmp(next, text) == 0) rc = 0;   // already in the wanted state
    else rc = safe_write_text(path, next);
    free(next);
    free(text);
    return rc;
}
