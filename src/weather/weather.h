#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Current weather + daily forecast from Open-Meteo (free, no API key).
// GET https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
//       &current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day
//       &daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max
//       &timezone=auto&forecast_days=5
// + air quality (US AQI) from https://air-quality-api.open-meteo.com/v1/air-quality?...&current=us_aqi

#define WEATHER_DAYS 5

struct weather_day_t {
    int   wday;      // 0=Sun .. 6=Sat, -1 unknown
    int   code;      // WMO weather code, -1 unknown
    float tmax, tmin;
    int   rain_pct;  // max precipitation probability %, -1 unknown
};

struct weather_t {
    bool  valid;           // current temperature known
    float temp_c;
    float feels_c;         // apparent temperature, NAN unknown
    int   code;            // current WMO weather code, -1 unknown
    int   humidity;        // %, -1 unknown
    float wind_kmh;        // NAN unknown
    bool  is_day;
    int   aqi;             // US AQI, -1 unknown

    bool  forecast_valid;
    weather_day_t days[WEATHER_DAYS]; // [0] = today

    time_t updated;        // UTC epoch of last successful update, 0 = never
    int    last_http;      // HTTP status of the last attempt (200 = ok)
};

typedef void (*weather_cb_t)(const weather_t &w);

void weather_begin(float lat, float lon);
void weather_loop();                       // call every loop(); polls every ~15 min
const weather_t &weather_get();
void weather_set_callback(weather_cb_t cb);
void weather_set_location(float lat, float lon);   // takes effect with an immediate refresh

// Parses an Open-Meteo JSON response into w (keeps fields it can't find).
// Plain C++ - no Arduino dependency, so it can be unit-tested on a PC.
bool weather_parse_json(const char *body, weather_t &w);
// Air-quality response ({"current":{"us_aqi":96,...}}) -> w.aqi
bool weather_parse_aqi(const char *body, weather_t &w);

// US AQI -> Vietnamese level ("Tốt", "Trung bình", "Kém", "Xấu", "Rất xấu", "Nguy hại")
// and its color (0xRRGGBB).
const char *weather_aqi_text(int aqi);
uint32_t weather_aqi_color(int aqi);

// WMO weather code -> short Vietnamese text (UTF-8: "Có mây", "Mưa", ...)
const char *weather_code_text(int code);
