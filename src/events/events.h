#pragma once
#include <stdint.h>
#include <stdbool.h>

// Personal countdowns (birthdays, anniversaries, death anniversaries, trips...),
// entered on the settings page, saved in NVS.
//
//   EV_SOLAR : every year on day/month (solar)
//   EV_LUNAR : every year on day/month of the lunar calendar (e.g. "giỗ")
//   EV_ONCE  : one solar date (day/month/year), gone once it has passed
//
// For yearly events `year` is optional: the year it started (birth year,
// wedding year...) -> "lần thứ N" / "N tuổi" on screen.

#define EVENT_MAX  8
#define EVENT_NAME 40

enum { EV_SOLAR = 0, EV_LUNAR = 1, EV_ONCE = 2 };

struct event_t {
    char     name[EVENT_NAME];  // UTF-8
    uint8_t  type, day, month;
    uint16_t year;              // EV_ONCE: the date's year; yearly: start year or 0
};

struct event_next_t {
    int  idx;        // index into the stored list
    long jd;         // next solar date (JD)
    long days;       // days from today (0 = today)
    int  count;      // anniversary number (0 = unknown)
};

void events_begin();
int  events_count();
const event_t &events_get(int i);
void events_set(const event_t *list, int n);   // replace all + save

// Upcoming occurrences from today, soonest first. Returns how many were written.
int events_upcoming(long today_jd, event_next_t *out, int max);
