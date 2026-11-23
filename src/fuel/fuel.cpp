#include "fuel.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

#define POLL_MS  (20UL * 60 * 1000)   // prices change about once a week (Thursday 15:00)
#define RETRY_MS (3UL * 60 * 1000)

// Petrolimex's CMS search API (the endpoint their own website uses for the price table).
// x-request = base64 of {"FilterBy":{"And":[{"SystemID":...},{"RepositoryID":...},
//   {"RepositoryEntityID":...},{"Status":{"Equals":"Published"}}]},"SortBy":{"LastModified":"Descending"},...}
static const char *PLX_URL =
    "https://portals.petrolimex.com.vn/~apis/portals/cms.item/search?x-request="
    "eyJGaWx0ZXJCeSI6eyJBbmQiOlt7IlN5c3RlbUlEIjp7IkVxdWFscyI6IjY3ODNkYzEyNzFmZjQ0OWU5NWI3NGE5NTIwOTY0MTY5In19LHsiUmVwb3NpdG9yeUlEIjp7IkVxdWFscyI6ImE5NTQ1MWUyM2I0NzRmZTU4ODZiZmI3Y2Y4NDNmNTNjIn19LHsiUmVwb3NpdG9yeUVudGl0eUlEIjp7IkVxdWFscyI6IjM4MDEzNzhmZTFlMDQ1YjFhZmExMGRlN2M1Nzc2MTI0In19LHsiU3RhdHVzIjp7IkVxdWFscyI6IlB1Ymxpc2hlZCJ9fV19LCJTb3J0QnkiOnsiTGFzdE1vZGlmaWVkIjoiRGVzY2VuZGluZyJ9LCJQYWdpbmF0aW9uIjp7IlRvdGFsUmVjb3JkcyI6LTEsIlRvdGFsUGFnZXMiOjAsIlBhZ2VTaXplIjowLCJQYWdlTnVtYmVyIjowfX0";
static const char *VIETFUEL_URL = "https://vietfuel-api.tranqui.workers.dev/api/fuel-prices/petrolimex";

static const char *UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0 Safari/537.36";

static fuel_t g_f;
static fuel_cb_t g_cb = nullptr;
static uint32_t g_last = 0;
static bool g_pending = true, g_last_ok = false;

// What is kept in NVS between reboots
struct stored_t {
    int32_t price[FUEL_N];
    int32_t delta[FUEL_N];
    char    eff[20];
    int64_t changed_at;
    uint8_t has_delta;
};

void fuel_set_callback(fuel_cb_t cb) { g_cb = cb; }
const fuel_t &fuel_get() { return g_f; }

static void save()
{
    stored_t s = {};
    for (int i = 0; i < FUEL_N; i++) { s.price[i] = g_f.price[i]; s.delta[i] = g_f.delta[i]; }
    strlcpy(s.eff, g_f.effective, sizeof(s.eff));
    s.changed_at = g_f.changed_at;
    s.has_delta = g_f.has_delta;
    Preferences p;
    p.begin("fuel", false);
    p.putBytes("st5", &s, sizeof(s));   // "st5": 5 products (the 3-product "st" is ignored)
    p.end();
}

void fuel_begin()
{
    memset(&g_f, 0, sizeof(g_f));
    stored_t s;
    Preferences p;
    p.begin("fuel", false);
    bool have = p.getBytesLength("st5") == sizeof(s) && p.getBytes("st5", &s, sizeof(s)) == sizeof(s);
    p.end();
    long sum = 0;
    for (int i = 0; i < FUEL_N; i++) sum += have ? s.price[i] : 0;
    if (have && sum > 0) {
        // show the last known prices right away; refreshed on the first fetch
        for (int i = 0; i < FUEL_N; i++) { g_f.price[i] = s.price[i]; g_f.delta[i] = s.delta[i]; }
        strlcpy(g_f.effective, s.eff, sizeof(g_f.effective));
        g_f.changed_at = (time_t)s.changed_at;
        g_f.has_delta = s.has_delta;
        g_f.ok = true;
        g_f.src = "NVS";
    }
}

static int https_get(const char *url, String &out, bool plx)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(12);
    HTTPClient http;
    http.setConnectTimeout(12000);
    http.setTimeout(15000);
    http.setReuse(false);
    if (!http.begin(client, url)) return -1;
    http.setUserAgent(UA);
    http.addHeader("Accept", "application/json");
    if (plx) http.addHeader("Referer", "https://www.petrolimex.com.vn/");
    int code = http.GET();
    if (code == 200) out = http.getString();
    http.end();
    return code;
}

// New prices in -> detect an adjustment, update deltas, save.
static void apply(const long price[FUEL_N], const char *eff, const char *src)
{
    g_f.just_changed = false;
    bool had = false;
    for (int i = 0; i < FUEL_N; i++) had |= g_f.ok && g_f.price[i] != 0;
    // a fallback source still on the previous period must not "undo" a change
    if (had && eff[0] && g_f.effective[0] && strncmp(eff, g_f.effective, 10) < 0) {
        Serial.printf("[fuel] %s has an older period (%s < %s), ignored\n", src, eff, g_f.effective);
        return;
    }
    bool changed = false;
    for (int i = 0; i < FUEL_N; i++)
        if (price[i] && g_f.price[i] && price[i] != g_f.price[i]) changed = true;

    if (changed) {
        for (int i = 0; i < FUEL_N; i++)
            g_f.delta[i] = (price[i] && g_f.price[i]) ? price[i] - g_f.price[i] : 0;
        g_f.has_delta = true;
        g_f.changed_at = time(nullptr);
        g_f.just_changed = true;
        Serial.printf("[fuel] adjustment: 95-III %+ld, 95-V %+ld, E5 %+ld, DO 0,05S %+ld, DO 0,001S %+ld\n",
                      g_f.delta[0], g_f.delta[1], g_f.delta[2], g_f.delta[3], g_f.delta[4]);
    }
    for (int i = 0; i < FUEL_N; i++)
        if (price[i]) g_f.price[i] = price[i];
    if (eff[0]) strlcpy(g_f.effective, eff, sizeof(g_f.effective));
    g_f.ok = true;
    g_f.src = src;
    if (changed || !had) save();
}

static bool poll()
{
    String body;
    long price[FUEL_N];
    char eff[20];

    int code = https_get(PLX_URL, body, true);
    if (code == 200 && fuel_parse_petrolimex(body.c_str(), price, eff, sizeof(eff))) {
        apply(price, eff, "Petrolimex");
        return true;
    }
    Serial.printf("[fuel] Petrolimex failed (%d), trying VietFuel\n", code == 200 ? -2 : code);
    body = String();
    int code2 = https_get(VIETFUEL_URL, body, false);
    if (code2 == 200 && fuel_parse_vietfuel(body.c_str(), price, eff, sizeof(eff))) {
        apply(price, eff, "VietFuel");
        return true;
    }
    g_f.last_error = code != 200 ? code : (code2 != 200 ? code2 : -2);
    Serial.printf("[fuel] all sources failed (%d)\n", g_f.last_error);
    return false;
}

void fuel_loop()
{
    if (WiFi.status() != WL_CONNECTED) return;
    if (!g_pending && millis() - g_last < (g_last_ok ? POLL_MS : RETRY_MS)) return;
    g_pending = false;
    g_last = millis();
    g_last_ok = poll();
    if (g_last_ok) {
        g_f.last_error = 0;
        g_f.fetched = time(nullptr);
    }
    if (g_cb) g_cb(g_f);
    g_f.just_changed = false; // report a change only once
}
