#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Vietnamese lunar calendar (UTC+7), Ho Ngoc Duc's algorithm
// (https://www.informatik.uni-leipzig.de/~duc/amlich/). Plain C++, no Arduino
// dependency, so it can be tested on a PC.

struct lunar_date_t {
    int  day, month, year;
    bool leap;      // leap month ("thang nhuan")
    long jd;        // Julian day number of the solar date
};

long jd_from_solar(int dd, int mm, int yy);
void solar_from_jd(long jd, int &dd, int &mm, int &yy);
int  weekday_from_jd(long jd);                   // 0 = Sunday .. 6 = Saturday

lunar_date_t lunar_from_solar(int dd, int mm, int yy);

// Can Chi (UTF-8, e.g. "Bính Ngọ")
void lunar_year_canchi(int lunar_year, char *out, size_t n);
void lunar_month_canchi(int lunar_month, int lunar_year, char *out, size_t n);
void lunar_day_canchi(long jd, char *out, size_t n);

// Upcoming dates from `today_jd` (today included).
struct lunar_event_t {
    long jd;
    const char *name;  // UTF-8, e.g. "Rằm", "Tết Trung Thu"
};

#define LUNAR_NEXT_HOLIDAYS 4

struct lunar_upcoming_t {
    lunar_event_t ram;      // next 15th
    lunar_event_t mung1;    // next 1st
    lunar_event_t holiday;  // next holiday (lunar or solar)
    const char *today_holiday; // holiday falling today, or nullptr
    // the next holidays in date order (today's included)
    lunar_event_t holidays[LUNAR_NEXT_HOLIDAYS];
    bool holiday_lunar[LUNAR_NEXT_HOLIDAYS];   // true = lunar-calendar holiday
    int n_holidays;
};

// Slow-ish (a few dozen new-moon calculations): call once per day.
void lunar_upcoming(long today_jd, lunar_upcoming_t &out);

// Tet (Mung 1 thang Gieng): JD of the next one, or of the current one while
// today is Mung 1..3. lunar_year = the year that Tet starts. 0 if not found.
long lunar_tet_jd(long today_jd, int &lunar_year);

// Next solar date (JD, today included) of lunar day/month (regular month, not
// the leap one). Day 30 in a 29-day month falls on that month's last day.
// lunar_year = the lunar year of that date. 0 if not found.
long lunar_next_date(long today_jd, int lday, int lmonth, int &lunar_year);

// Solar term (tiet khi) of a day, UTF-8, e.g. "Lập đông", "Tiểu tuyết".
const char *lunar_solar_term(long jd);
