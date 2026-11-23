#include "events.h"
#include "../lunar/lunar.h"
#include <string.h>

#ifdef ARDUINO
#include <Preferences.h>
#endif

static event_t g_ev[EVENT_MAX];
static int g_n = 0;

int events_count() { return g_n; }
const event_t &events_get(int i) { return g_ev[i]; }

static bool valid(const event_t &e)
{
    if (!e.name[0] || e.type > EV_ONCE || e.day < 1 || e.month < 1 || e.month > 12) return false;
    if (e.type == EV_LUNAR) return e.day <= 30;
    if (e.day > 31) return false;
    return e.type != EV_ONCE || (e.year >= 2000 && e.year <= 2100);
}

void events_begin()
{
    g_n = 0;
#ifdef ARDUINO
    Preferences p;
    p.begin("events", false);
    size_t len = p.getBytes("list", g_ev, sizeof(g_ev));
    p.end();
    int n = (int)(len / sizeof(event_t));
    for (int i = 0; i < n; i++)
        if (valid(g_ev[i])) g_ev[g_n++] = g_ev[i];
#endif
}

void events_set(const event_t *list, int n)
{
    g_n = 0;
    for (int i = 0; i < n && g_n < EVENT_MAX; i++)
        if (valid(list[i])) {
            g_ev[g_n] = list[i];
            g_ev[g_n].name[EVENT_NAME - 1] = 0;
            g_n++;
        }
#ifdef ARDUINO
    Preferences p;
    p.begin("events", false);
    if (g_n) p.putBytes("list", g_ev, g_n * sizeof(event_t));
    else p.remove("list");
    p.end();
#endif
}

// Next solar occurrence of day/month (Feb 29 -> Feb 28 in other years).
static long next_solar(long today, int d, int m, int &year)
{
    int td, tm, ty;
    solar_from_jd(today, td, tm, ty);
    for (int y = ty; y <= ty + 1; y++) {
        int dd = d;
        if (m == 2 && d == 29) {
            bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
            if (!leap) dd = 28;
        }
        long jd = jd_from_solar(dd, m, y);
        int cd, cm, cy;
        solar_from_jd(jd, cd, cm, cy);
        if (cm != m) continue; // e.g. 31/4 does not exist
        if (jd >= today) { year = y; return jd; }
    }
    return 0;
}

int events_upcoming(long today, event_next_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < g_n; i++) {
        const event_t &e = g_ev[i];
        long jd = 0;
        int year = 0;
        if (e.type == EV_ONCE) {
            jd = jd_from_solar(e.day, e.month, e.year);
            if (jd < today) continue;   // already passed
        } else if (e.type == EV_LUNAR) {
            jd = lunar_next_date(today, e.day, e.month, year);
        } else {
            jd = next_solar(today, e.day, e.month, year);
        }
        if (!jd) continue;
        event_next_t x;
        x.idx = i;
        x.jd = jd;
        x.days = jd - today;
        x.count = (e.type != EV_ONCE && e.year && year > e.year) ? year - e.year : 0;
        // insertion sort by date
        int j = n < max ? n : max - 1;
        if (n >= max && x.jd >= out[max - 1].jd) continue;
        while (j > 0 && out[j - 1].jd > x.jd) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = x;
        if (n < max) n++;
    }
    return n;
}
