#include "market.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <math.h>

#define TZ_OFFSET_SEC (7 * 3600)

#define GOLD_EVERY_MS      (10UL * 60 * 1000)
#define USD_EVERY_MS       (30UL * 60 * 1000)
#define BTC_VISIBLE_MS     (60UL * 1000)        // while the page is on screen
#define BTC_HIDDEN_MS      (10UL * 60 * 1000)
#define RETRY_MS           (2UL * 60 * 1000)    // after a failure

static const char *UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/129.0 Safari/537.36";

static market_t g_m;
static int g_code; // last failing HTTP status / -1 / -2 (see fail())
static market_cb_t g_cb = nullptr;
static char g_vnappmob_key[512] = "";

struct poller {
    uint32_t last;   // millis() of the last attempt
    bool ok;         // last attempt succeeded
    bool pending;    // never tried yet
};
static poller p_gold = {0, false, true}, p_usd = {0, false, true}, p_btc = {0, false, true};

void market_set_callback(market_cb_t cb) { g_cb = cb; }
const market_t &market_get() { return g_m; }

// ---- Price changes ----
// For gold bar, ring and USD the last sell price and the change at its last
// adjustment are kept in NVS, so "↑0,5" survives a reboot. Written only when a
// price actually changes (a few times a day at most).
enum { TRK_GOLD = 0, TRK_RING, TRK_USD, TRK_N };
struct tracked_t {
    double prev[TRK_N];   // last seen sell price, 0 = none
    double delta[TRK_N];  // change at the last adjustment
};
static tracked_t g_trk;

static void trk_save()
{
    Preferences p;
    p.begin("market", false);
    p.putBytes("trk", &g_trk, sizeof(g_trk));
    p.end();
}

// New sell price -> change to show
static double track(int i, double sell)
{
    double prev = g_trk.prev[i];
    if (fabs(sell - prev) >= 1) {
        if (prev > 0) g_trk.delta[i] = sell - prev;
        g_trk.prev[i] = sell;
        trk_save();
    }
    return g_trk.delta[i];
}

void market_begin()
{
    memset(&g_m, 0, sizeof(g_m));
    memset(&g_trk, 0, sizeof(g_trk));
    Preferences p;
    p.begin("market", false);
    if (p.getBytesLength("trk") == sizeof(g_trk)) p.getBytes("trk", &g_trk, sizeof(g_trk));
    p.end();
}

// One HTTPS request. Returns the HTTP status (or <0 on network error); body in `out` on 200.
static int https_request(const char *url, const char *post_body, String &out,
                         const char *const headers[][2] = nullptr, int n_headers = 0,
                         const char *ua = UA, uint16_t timeout_ms = 12000, bool http10 = false)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(12);

    HTTPClient http;
    http.setConnectTimeout(12000);
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    if (!http.begin(client, url)) return -1;
    http.useHTTP10(http10); // HTTP/1.0 also drops the odd "Accept-Encoding: identity;q=1,chunked;.." header
    http.setUserAgent(ua);
    for (int i = 0; i < n_headers; i++) http.addHeader(headers[i][0], headers[i][1]);

    int code = post_body ? http.POST((uint8_t *)post_body, strlen(post_body)) : http.GET();
    if (code == 200) out = http.getString();
    http.end();
    return code;
}

// One source failed (logged; the item only counts as failed when all its sources did)
static void fail(const char *src, int code)
{
    Serial.printf("[market] %s failed (%d)\n", src, code);
    g_code = code;
}

static void item_result(int &err, const char *item, int code)
{
    err = code;
    if (code) {
        g_m.last_error = code;
        g_m.last_error_src = item;
    } else if (!g_m.gold_err && !g_m.usd_err && !g_m.btc_err) {
        g_m.last_error = 0;
        g_m.last_error_src = nullptr;
    }
}

// ---- Gold: SJC's own price service, then vnappmob as a fallback ----
static bool fetch_gold_sjc()
{
    static const char *const H[][2] = {
        {"Content-Type", "application/x-www-form-urlencoded; charset=UTF-8"},
        {"Accept", "application/json, text/javascript, */*; q=0.01"},
        {"X-Requested-With", "XMLHttpRequest"},
        {"Referer", "https://sjc.com.vn/"},
        {"Origin", "https://sjc.com.vn"},
    };
    String body;
    int code = https_request("https://sjc.com.vn/GoldPrice/Services/PriceService.ashx",
                             "method=GetCurrentGoldPricesByBranch&BranchId=1", body, H, 5);
    double b, s;
    if (code == 200 && market_parse_sjc(body.c_str(), b, s)) {
        g_m.gold_buy = b;
        g_m.gold_sell = s;
        g_m.gold_src = "SJC";
        g_m.ring_ok = market_parse_sjc_ring(body.c_str(), b, s);
        if (g_m.ring_ok) { g_m.ring_buy = b; g_m.ring_sell = s; }
        return true;
    }
    fail("SJC", code == 200 ? -2 : code);
    return false;
}

static bool fetch_gold_vnappmob()
{
    String body;
    if (!g_vnappmob_key[0]) { // free short-lived key, kept in RAM only
        int code = https_request("https://api.vnappmob.com/api/request_api_key?scope=gold", nullptr, body);
        if (code != 200 || !market_parse_vnappmob_key(body.c_str(), g_vnappmob_key, sizeof(g_vnappmob_key))) {
            fail("vnappmob", code == 200 ? -2 : code);
            return false;
        }
    }
    char auth[540];
    snprintf(auth, sizeof(auth), "Bearer %s", g_vnappmob_key);
    const char *const H[][2] = {{"Authorization", auth}, {"Accept", "application/json"}};
    int code = https_request("https://api.vnappmob.com/api/v2/gold/sjc", nullptr, body, H, 2);
    double b, s;
    if (code == 200 && market_parse_vnappmob(body.c_str(), b, s)) {
        g_m.gold_buy = b;
        g_m.gold_sell = s;
        g_m.gold_src = "vnappmob";
        g_m.ring_ok = market_parse_vnappmob_ring(body.c_str(), b, s);
        if (g_m.ring_ok) { g_m.ring_buy = b; g_m.ring_sell = s; }
        return true;
    }
    if (code == 401 || code == 403) g_vnappmob_key[0] = 0; // key expired -> new one next time
    fail("vnappmob", code == 200 ? -2 : code);
    return false;
}

static bool fetch_gold()
{
    if (fetch_gold_sjc() || fetch_gold_vnappmob()) {
        item_result(g_m.gold_err, "Vàng", 0);
        g_m.gold_ok = true;
        g_m.gold_time = time(nullptr);
        g_m.gold_dsell = track(TRK_GOLD, g_m.gold_sell);
        if (g_m.ring_ok) g_m.ring_dsell = track(TRK_RING, g_m.ring_sell);
        return true;
    }
    item_result(g_m.gold_err, "Vàng", g_code);
    return false;
}

// ---- USD: Vietcombank JSON API ----
static bool usd_ok(double buy, double sell, time_t when)
{
    g_m.usd_ok = true;
    g_m.usd_buy = buy;
    g_m.usd_sell = sell;
    g_m.usd_dsell = track(TRK_USD, sell);
    g_m.usd_time = when;
    item_result(g_m.usd_err, "USD", 0);
    return true;
}

static bool fetch_usd()
{
    time_t now = time(nullptr);
    if (now < 1000000000) return false; // need today's date for the URL
    now += TZ_OFFSET_SEC;
    struct tm tm;
    gmtime_r(&now, &tm);
    char url[96];
    snprintf(url, sizeof(url), "https://www.vietcombank.com.vn/api/exchangerates?date=%04d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    // The site's bot filter often stalls the ESP32 (no answer -> HTTP -11, read
    // timeout), so this is only the fallback for when the PC isn't sending the
    // rate. Each try (every 2 min while failing) uses the next request style;
    // the one that works is kept.
    struct style_t { const char *ua; bool http10; };
    static const style_t STYLES[] = {
        {"curl/8.5.0", true}, {"curl/8.5.0", false}, {"ESP32HTTPClient", true}, {UA, true},
    };
    static int st = 0;
    static const char *const H[][2] = {{"Accept", "application/json"}};
    String body;
    double b, s;
    int code = https_request(url, nullptr, body, H, 1, STYLES[st].ua, 20000, STYLES[st].http10);
    if (code == 200 && market_parse_vcb(body.c_str(), b, s)) return usd_ok(b, s, time(nullptr));
    Serial.printf("[market] VCB (UA \"%s\", HTTP/1.%d): %d%s%.80s\n", STYLES[st].ua,
                  STYLES[st].http10 ? 0 : 1, code, code == 200 ? " body: " : "",
                  code == 200 ? body.c_str() : "");
    st = (st + 1) % (sizeof(STYLES) / sizeof(STYLES[0]));
    fail("VCB", code == 200 ? -2 : code);

    item_result(g_m.usd_err, "USD", g_code);
    return false;
}

// ---- BTC: Binance, CoinGecko as a fallback ----
static bool fetch_btc()
{
    String body;
    double p, c;
    int code = https_request("https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT", nullptr, body);
    bool ok = code == 200 && market_parse_binance(body.c_str(), p, c);
    if (!ok) {
        code = https_request("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin"
                             "&vs_currencies=usd&include_24hr_change=true", nullptr, body);
        ok = code == 200 && market_parse_coingecko(body.c_str(), p, c);
    }
    if (ok) {
        item_result(g_m.btc_err, "BTC", 0);
        g_m.btc_ok = true;
        g_m.btc_usd = p;
        g_m.btc_change = c;
        g_m.btc_time = time(nullptr);
        return true;
    }
    fail("BTC", code == 200 ? -2 : code);
    item_result(g_m.btc_err, "BTC", g_code);
    return false;
}

static bool due(const poller &p, uint32_t every)
{
    if (p.pending) return true;
    return millis() - p.last >= (p.ok ? every : RETRY_MS);
}

static void run(poller &p, bool (*fn)())
{
    p.pending = false;
    p.last = millis();
    p.ok = fn();
    if (g_cb) g_cb(g_m);
}

// ---- USD pushed by the PC (web server, core 1) -> applied here (network task) ----
static portMUX_TYPE g_push_mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_push_new = false;
static double g_push_buy, g_push_sell;
static time_t g_push_t, g_push_applied_t = 0;

void market_push_usd(double buy, double sell, time_t fetched_utc)
{
    if (!(buy > 1e4 && buy < 1e5 && sell > 1e4 && sell < 1e5)) return;
    portENTER_CRITICAL(&g_push_mux);
    g_push_buy = buy;
    g_push_sell = sell;
    g_push_t = fetched_utc;
    g_push_new = true;
    portEXIT_CRITICAL(&g_push_mux);
}

static void apply_pushed_usd()
{
    double b, s;
    time_t t;
    portENTER_CRITICAL(&g_push_mux);
    bool have = g_push_new;
    g_push_new = false;
    b = g_push_buy;
    s = g_push_sell;
    t = g_push_t;
    portEXIT_CRITICAL(&g_push_mux);
    if (!have) return;

    // the PC is sending the rate -> the board doesn't need to fetch it itself
    p_usd.pending = false;
    p_usd.ok = true;
    p_usd.last = millis();
    if (t == g_push_applied_t && g_m.usd_ok) return; // same fetch as before (sent every second)
    g_push_applied_t = t;
    usd_ok(b, s, t > 1000000000 ? t : time(nullptr));
    if (g_cb) g_cb(g_m);
}

void market_loop(bool page_visible)
{
    apply_pushed_usd();
    if (WiFi.status() != WL_CONNECTED) return;
    // At most one request per call, so the screen keeps responding.
    if (due(p_btc, page_visible ? BTC_VISIBLE_MS : BTC_HIDDEN_MS)) run(p_btc, fetch_btc);
    else if (due(p_gold, GOLD_EVERY_MS)) run(p_gold, fetch_gold);
    else if (due(p_usd, USD_EVERY_MS)) run(p_usd, fetch_usd);
}
