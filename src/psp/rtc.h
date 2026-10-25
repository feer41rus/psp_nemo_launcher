#ifndef NEMO_RTC_H
#define NEMO_RTC_H

#include "datetime.h"

int rtc_get_local(DateTime *out);                        // 0 ok
// Sets the PSP clock (UTC inside) from a local time. 0 ok, < 0 error.
int rtc_set_local(const DateTime *local, int tz_minutes);

#endif
