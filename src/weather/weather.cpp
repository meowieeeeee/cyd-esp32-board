#include "weather.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>

#define POLL_INTERVAL_MS  (15UL * 60UL * 1000UL) // 15 min
#define RETRY_INTERVAL_MS (2UL * 60UL * 1000UL)  // after a failed request

static weather_t g_w;
static weather_cb_t g_cb = nullptr;
static volatile float g_lat = 0, g_lon = 0; // set from the UI core, read by the network task
static uint32_t g_last_poll = 0;
static volatile bool g_pending = true;
static bool g_last_ok = false;

void weather_begin(float lat, float lon)
{
    memset(&g_w, 0, sizeof(g_w));
    g_w.code = -1;
    g_w.humidity = -1;
    g_w.wind_kmh = NAN;
    g_w.feels_c = NAN;
    g_w.aqi = -1;
    g_w.is_day = true;
    for (int i = 0; i < WEATHER_DAYS; i++) {
        g_w.days[i] = {-1, -1, NAN, NAN, -1};
    }
    g_lat = lat;
    g_lon = lon;
}

void weather_set_callback(weather_cb_t cb) { g_cb = cb; }

void weather_set_location(float lat, float lon)
{
    if (lat == g_lat && lon == g_lon) return;
    g_lat = lat;
    g_lon = lon;
    g_pending = true; // refresh on the next weather_loop()
}
const weather_t &weather_get() { return g_w; }

// GET url -> body. Returns the HTTP status (or a negative HTTPClient error).
static int http_get(const char *url, String &body)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPClient https;
    https.setConnectTimeout(15000);
    https.setTimeout(15000);
    https.setReuse(false);
    if (!https.begin(client, url)) return -1;
    int code = https.GET();
    if (code == 200) body = https.getString();
    https.end();
    return code;
}

// Air quality (separate Open-Meteo endpoint). A failure keeps the last value.
static void poll_aqi()
{
    char url[200];
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi",
             g_lat, g_lon);
    static float last_lat = NAN, last_lon = NAN;
    if (g_lat != last_lat || g_lon != last_lon) { // new place: don't show the old one's AQI
        g_w.aqi = -1;
        last_lat = g_lat;
        last_lon = g_lon;
    }
    String body;
    int code = http_get(url, body);
    if (code == 200 && weather_parse_aqi(body.c_str(), g_w))
        Serial.printf("[weather] AQI %d\n", g_w.aqi);
    else
        Serial.printf("[weather] AQI loi (HTTP %d)\n", code);
}

static void do_poll()
{
    if (WiFi.status() != WL_CONNECTED) return;

    char url[360];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=auto&forecast_days=%d",
             g_lat, g_lon, WEATHER_DAYS);

    String body;
    int code = http_get(url, body);
    g_w.last_http = code;
    g_last_ok = false;
    if (code == 200) {
        if (weather_parse_json(body.c_str(), g_w)) {
            g_last_ok = true;
            g_w.updated = time(nullptr);
            Serial.printf("[weather] %.1fC code=%d, forecast %s\n", g_w.temp_c, g_w.code,
                          g_w.forecast_valid ? "ok" : "missing");
        } else {
            Serial.println("[weather] parse failed");
        }
    } else {
        Serial.printf("[weather] HTTP %d\n", code);
    }
    body = String();      // free before the second TLS connection
    if (g_last_ok) poll_aqi();
    if (g_cb) g_cb(g_w);
}

void weather_loop()
{
    if (WiFi.status() != WL_CONNECTED) return;
    uint32_t now = millis();
    uint32_t interval = g_last_ok ? POLL_INTERVAL_MS : RETRY_INTERVAL_MS;
    if (g_pending || now - g_last_poll >= interval) {
        g_pending = false;
        g_last_poll = now;
        do_poll();
    }
}
