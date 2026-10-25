#ifndef NEMO_ARKSETTINGS_H
#define NEMO_ARKSETTINGS_H

// Return a malloc'd copy of ARK's SETTINGS.TXT text with the global launcher line set to
// "always, launcher, on|off". Only lines whose first field is "always" and second field
// "launcher" are replaced; if none exists the line is appended. Other bytes are untouched.
char *arksettings_set_launcher(const char *text, int on);

// Apply the launcher line to a SETTINGS.TXT on disk, crash-safely (see safefile.h).
// - missing file + off: nothing is written (ARK then uses its flash1 defaults);
// - missing file + on: starts from fallback_path (ARK's flash1 copy) when readable, else empty;
// - unreadable/too large file: nothing is written, returns -1.
// Returns 0 on success or when nothing needed to change.
int arksettings_update_file(const char *path, const char *fallback_path, int on);

// Generic "always, <key>, on|off" line: value of the last matching line, default_on if none.
int arksettings_get(const char *text, const char *key, int default_on);
// Same as arksettings_set_launcher for any key.
char *arksettings_set(const char *text, const char *key, int on);
// Read a key from SETTINGS.TXT (fallback file when missing, then default_on).
int arksettings_read_key_file(const char *path, const char *fallback_path, const char *key, int default_on);
// arksettings_update_file for any key; a missing file is left alone when on == default_on.
int arksettings_update_key_file(const char *path, const char *fallback_path, const char *key, int on, int default_on);

#endif
