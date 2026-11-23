// Open-Meteo JSON parsing without a JSON library. The response is small and
// flat, so we anchor on the "current"/"daily" objects and read values by key.
#include "weather.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Finds `"key"` inside [p, end) and returns a pointer just after the ':'.
static const char *find_key(const char *p, const char *end, const char *key)
{
    size_t klen = strlen(key);
    while (p && p < end) {
        const char *q = strstr(p, key);
        if (!q || q >= end) return nullptr;
        // must be a quoted key followed by ':'
        if (q > p && q[-1] == '"' && q[klen] == '"') {
            const char *c = q + klen + 1;
            while (*c == ' ') c++;
            if (*c == ':') return c + 1;
        }
        p = q + klen;
    }
    return nullptr;
}

// Start of the object for a top-level key like "current" / "daily".
// ("current_units" / "daily_units" don't match because the quote must follow.)
static const char *object_start(const char *body, const char *key)
{
    const char *v = find_key(body, body + strlen(body), key);
    if (!v) return nullptr;
    while (*v == ' ') v++;
    return *v == '{' ? v : nullptr;
}

static const char *object_end(const char *start)
{
    int depth = 0;
    for (const char *c = start; *c; c++) {
        if (*c == '{') depth++;
        else if (*c == '}' && --depth == 0) return c;
    }
    return start + strlen(start);
}

static bool read_number(const char *obj, const char *end, const char *key, double &out)
{
    const char *v = find_key(obj, end, key);
    if (!v) return false;
    while (*v == ' ') v++;
    if (strncmp(v, "null", 4) == 0) return false;
    char *e;
    double d = strtod(v, &e);
    if (e == v) return false;
    out = d;
    return true;
}

// Reads up to n numbers from `"key":[a,b,null,...]`. null -> NAN. Returns count.
static int read_array(const char *obj, const char *end, const char *key, double *out, int n)
{
    const char *v = find_key(obj, end, key);
    if (!v) return 0;
    while (*v == ' ') v++;
    if (*v != '[') return 0;
    v++;
    int k = 0;
    while (k < n && *v && *v != ']') {
        while (*v == ' ' || *v == ',') v++;
        if (*v == ']') break;
        if (strncmp(v, "null", 4) == 0) {
            out[k++] = NAN;
            v += 4;
        } else {
            char *e;
            double d = strtod(v, &e);
            if (e == v) break;
            out[k++] = d;
            v = e;
        }
    }
    return k;
}

// "2026-11-21" -> weekday (0=Sun). Howard Hinnant's days_from_civil.
static int weekday_of(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468; // days since 1970-01-01 (Thu)
    return (int)(((days % 7) + 11) % 7);                 // 1970-01-01 -> 4
}

static int read_weekdays(const char *obj, const char *end, int *out, int n)
{
    const char *v = find_key(obj, end, "time");
    if (!v) return 0;
    while (*v == ' ') v++;
    if (*v != '[') return 0;
    int k = 0;
    while (k < n) {
        const char *q = strchr(v, '"');
        if (!q || q >= end) break;
        int y, m, d;
        if (sscanf(q + 1, "%d-%d-%d", &y, &m, &d) != 3) break;
        out[k++] = weekday_of(y, m, d);
        const char *close = strchr(q + 1, '"');
        if (!close) break;
        v = close + 1;
        while (*v == ' ' || *v == ',') v++;
        if (*v == ']') break;
    }
    return k;
}

bool weather_parse_json(const char *body, weather_t &w)
{
    bool ok = false;
    double d;

    const char *cur = object_start(body, "current");
    if (cur) {
        const char *end = object_end(cur);
        if (read_number(cur, end, "temperature_2m", d)) { w.temp_c = (float)d; w.valid = true; ok = true; }
        w.feels_c  = read_number(cur, end, "apparent_temperature", d) ? (float)d : NAN;
        w.code     = read_number(cur, end, "weather_code", d) ? (int)d : -1;
        w.humidity = read_number(cur, end, "relative_humidity_2m", d) ? (int)lround(d) : -1;
        w.wind_kmh = read_number(cur, end, "wind_speed_10m", d) ? (float)d : NAN;
        w.is_day   = read_number(cur, end, "is_day", d) ? d != 0 : true;
    }

    const char *daily = object_start(body, "daily");
    if (daily) {
        const char *end = object_end(daily);
        double code[WEATHER_DAYS], tmax[WEATHER_DAYS], tmin[WEATHER_DAYS], rain[WEATHER_DAYS];
        int wd[WEATHER_DAYS];
        int n  = read_array(daily, end, "temperature_2m_max", tmax, WEATHER_DAYS);
        int n2 = read_array(daily, end, "temperature_2m_min", tmin, WEATHER_DAYS);
        int nc = read_array(daily, end, "weather_code", code, WEATHER_DAYS);
        int nr = read_array(daily, end, "precipitation_probability_max", rain, WEATHER_DAYS);
        int nw = read_weekdays(daily, end, wd, WEATHER_DAYS);
        if (n2 < n) n = n2;
        if (n > 0) {
            for (int i = 0; i < WEATHER_DAYS; i++) {
                weather_day_t &o = w.days[i];
                bool have = i < n;
                o.tmax     = have ? (float)tmax[i] : NAN;
                o.tmin     = have ? (float)tmin[i] : NAN;
                o.code     = (have && i < nc && !isnan(code[i])) ? (int)code[i] : -1;
                o.rain_pct = (have && i < nr && !isnan(rain[i])) ? (int)lround(rain[i]) : -1;
                o.wday     = (have && i < nw) ? wd[i] : -1;
            }
            w.forecast_valid = true;
            ok = true;
        }
    }
    return ok;
}

bool weather_parse_aqi(const char *body, weather_t &w)
{
    const char *cur = object_start(body, "current");
    if (!cur) return false;
    double d;
    if (!read_number(cur, object_end(cur), "us_aqi", d)) return false;
    w.aqi = (int)lround(d);
    return true;
}

// US EPA bands; the names follow Vietnam's VN_AQI scale.
const char *weather_aqi_text(int a)
{
    if (a < 0)    return "--";
    if (a <= 50)  return "Tốt";
    if (a <= 100) return "Trung bình";
    if (a <= 150) return "Kém";
    if (a <= 200) return "Xấu";
    if (a <= 300) return "Rất xấu";
    return "Nguy hại";
}

uint32_t weather_aqi_color(int a)
{
    if (a < 0)    return 0x3A3547;
    if (a <= 50)  return 0x6CC24A;
    if (a <= 100) return 0xF6C641;
    if (a <= 150) return 0xF08A3C;
    if (a <= 200) return 0xE5534B;
    if (a <= 300) return 0x9B59B6;
    return 0x8E2C3C;
}

const char *weather_code_text(int c)
{
    switch (c) {
    case 0:                     return "Trời quang";
    case 1:                     return "Ít mây";
    case 2:                     return "Có mây";
    case 3:                     return "Nhiều mây";
    case 45: case 48:           return "Sương mù";
    case 51: case 53: case 55:
    case 56: case 57:           return "Mưa phùn";
    case 61: case 63:           return "Mưa";
    case 65:                    return "Mưa to";
    case 66: case 67:           return "Mưa băng";
    case 71: case 73: case 75:
    case 77:                    return "Tuyết";
    case 80: case 81:           return "Mưa rào";
    case 82:                    return "Mưa rào to";
    case 85: case 86:           return "Mưa tuyết";
    case 95:                    return "Dông";
    case 96: case 99:           return "Dông, mưa đá";
    default:                    return "--";
    }
}
