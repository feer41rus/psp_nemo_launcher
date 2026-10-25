#ifndef NEMO_DATETIME_H
#define NEMO_DATETIME_H

#include <stddef.h>

// Date/time editing and formatting for Settings › Time (registry values of the PSP).
typedef struct { int year, month, day, hour, minute; } DateTime;   // month 1..12
typedef enum { DF_YMD, DF_MDY, DF_DMY } DateFormat;                // /CONFIG/DATE date_format 0, 1, 2

#define DT_YEAR_MIN 2020
#define DT_YEAR_MAX 2099

int  datetime_days_in_month(int year, int month);
void datetime_clamp_day(DateTime *d);
// field 0..4 = year, month, day, hour, minute; each wraps on its own (no carry), year 2020..2099.
void datetime_step(DateTime *d, int field, int delta);
// Editor positions -> fields (0..4 = Y M D h m) in the order of the date format.
void datetime_field_order(DateFormat f, int order[5]);
void datetime_format_date(const DateTime *d, DateFormat f, char *out, size_t cap);   // "2026/10/05" ...
void datetime_format_time(const DateTime *d, int h12, char *out, size_t cap);        // "15:05" / "3:05 PM"
void datetime_local_to_utc(const DateTime *local, int tz_minutes, DateTime *utc);
void datetime_utc_to_local(const DateTime *utc, int tz_minutes, DateTime *local);
int  datetime_clock_lost(const DateTime *d);                        // year < 2020: battery ran flat

typedef struct { int minutes; const char *city_ru, *city_en; } TimeZone;
extern const TimeZone TIME_ZONES[];
extern const int TIME_ZONE_COUNT;

#endif
