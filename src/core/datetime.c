#include "datetime.h"
#include <stdio.h>

const TimeZone TIME_ZONES[] = {
    { -720, "Линия перемены дат", "Baker Island" }, { -660, "Самоа (США)", "American Samoa" },
    { -600, "Гавайи", "Hawaii" }, { -540, "Аляска", "Alaska" }, { -480, "Лос-Анджелес", "Los Angeles" },
    { -420, "Денвер", "Denver" }, { -360, "Мехико, Чикаго", "Mexico City, Chicago" },
    { -300, "Нью-Йорк", "New York" }, { -240, "Сантьяго, Каракас", "Santiago, Caracas" },
    { -210, "Ньюфаундленд", "Newfoundland" }, { -180, "Буэнос-Айрес", "Buenos Aires" },
    { -120, "Южная Георгия", "South Georgia" }, { -60, "Азорские острова", "Azores" },
    { 0, "Лондон, Лиссабон", "London, Lisbon" }, { 60, "Берлин, Париж", "Berlin, Paris" },
    { 120, "Калининград, Киев", "Kaliningrad, Kyiv" }, { 180, "Москва, Минск", "Moscow, Minsk" },
    { 210, "Тегеран", "Tehran" }, { 240, "Самара, Дубай", "Samara, Dubai" }, { 270, "Кабул", "Kabul" },
    { 300, "Екатеринбург", "Yekaterinburg" }, { 330, "Индия", "India" }, { 345, "Катманду", "Kathmandu" },
    { 360, "Омск, Алматы", "Omsk, Almaty" }, { 390, "Янгон", "Yangon" },
    { 420, "Новосибирск, Красноярск", "Novosibirsk, Krasnoyarsk" }, { 480, "Иркутск, Пекин", "Irkutsk, Beijing" },
    { 540, "Якутск, Токио", "Yakutsk, Tokyo" }, { 570, "Аделаида", "Adelaide" },
    { 600, "Владивосток, Сидней", "Vladivostok, Sydney" }, { 660, "Магадан", "Magadan" },
    { 720, "Камчатка, Окленд", "Kamchatka, Auckland" }, { 780, "Тонга", "Tonga" },
    { 840, "Остров Рождества", "Kiritimati" },
};
const int TIME_ZONE_COUNT = (int)(sizeof TIME_ZONES / sizeof TIME_ZONES[0]);

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int datetime_days_in_month(int year, int month) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return month == 2 && is_leap(year) ? 29 : days[month - 1];
}

void datetime_clamp_day(DateTime *d) {
    int max = datetime_days_in_month(d->year, d->month);
    if (d->day > max) d->day = max;
    if (d->day < 1) d->day = 1;
}

static int wrap(int v, int lo, int hi) {
    int n = hi - lo + 1;
    return lo + ((v - lo) % n + n) % n;
}

void datetime_step(DateTime *d, int field, int delta) {
    switch (field) {
    case 0: d->year = wrap(d->year + delta, DT_YEAR_MIN, DT_YEAR_MAX); break;
    case 1: d->month = wrap(d->month + delta, 1, 12); break;
    case 2: d->day = wrap(d->day + delta, 1, datetime_days_in_month(d->year, d->month)); break;
    case 3: d->hour = wrap(d->hour + delta, 0, 23); break;
    default: d->minute = wrap(d->minute + delta, 0, 59); break;
    }
    datetime_clamp_day(d);
}

void datetime_field_order(DateFormat f, int order[5]) {
    static const int ORDERS[3][3] = { { 0, 1, 2 }, { 1, 2, 0 }, { 2, 1, 0 } };   // YMD, MDY, DMY
    for (int i = 0; i < 3; i++) order[i] = ORDERS[f][i];
    order[3] = 3;
    order[4] = 4;
}

void datetime_format_date(const DateTime *d, DateFormat f, char *out, size_t cap) {
    if (f == DF_MDY) snprintf(out, cap, "%02d/%02d/%04d", d->month, d->day, d->year);
    else if (f == DF_DMY) snprintf(out, cap, "%02d/%02d/%04d", d->day, d->month, d->year);
    else snprintf(out, cap, "%04d/%02d/%02d", d->year, d->month, d->day);
}

void datetime_format_time(const DateTime *d, int h12, char *out, size_t cap) {
    if (!h12) {
        snprintf(out, cap, "%02d:%02d", d->hour, d->minute);
        return;
    }
    int h = d->hour % 12;
    snprintf(out, cap, "%d:%02d %s", h ? h : 12, d->minute, d->hour < 12 ? "AM" : "PM");
}

// Days since 1970-01-01 (proleptic Gregorian), and back (H. Hinnant's civil algorithms).
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, DateTime *t) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    t->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    t->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    t->year = (int)(yoe + era * 400 + (t->month <= 2));
}

static void shift(const DateTime *in, int minutes, DateTime *out) {
    long total = (days_from_civil(in->year, in->month, in->day) * 24 + in->hour) * 60 + in->minute + minutes;
    long days = total >= 0 ? total / 1440 : -((-total + 1439) / 1440);
    long rest = total - days * 1440;
    civil_from_days(days, out);
    out->hour = (int)(rest / 60);
    out->minute = (int)(rest % 60);
}

void datetime_local_to_utc(const DateTime *local, int tz_minutes, DateTime *utc) { shift(local, -tz_minutes, utc); }
void datetime_utc_to_local(const DateTime *utc, int tz_minutes, DateTime *local) { shift(utc, tz_minutes, local); }

int datetime_clock_lost(const DateTime *d) { return d->year < 2020; }
