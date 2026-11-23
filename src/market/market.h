#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stddef.h>

// Prices for the "Gia ca" page (tabs Vang / Xang / Ty gia):
//   - SJC gold bar (vang mieng SJC 1L) and SJC ring 99.99 (nhan 1-5 chi), VND per luong
//       sjc.com.vn price service (answers Vietnamese IPs only), fallback vnappmob.com
//   - USD/VND, Vietcombank JSON API (buy transfer / sell). Fetched by the PC
//       (tools/pc_monitor.py sends it with the PC stats, see market_push_usd())
//       and, while the PC isn't sending it, by the board itself. Vietcombank's
//       bot filter often stalls the ESP32's own requests (HTTP -11, timeout).
//   - BTC/USD + 24h change, Binance, fallback CoinGecko
//
// Each source is polled on its own timer, one HTTPS request per loop() at most.

struct market_t {
    bool   gold_ok;
    double gold_buy, gold_sell;   // VND / luong
    double gold_dsell;            // change of the sell price at its last adjustment, 0 = none / unknown
    time_t gold_time;             // when fetched (UTC epoch)
    const char *gold_src;         // "SJC" / "vnappmob"

    bool   ring_ok;               // ring gold 99.99, same source and time as the bar
    double ring_buy, ring_sell, ring_dsell;

    bool   usd_ok;
    double usd_buy, usd_sell;     // VND per USD (buy = transfer)
    double usd_dsell;
    time_t usd_time;

    bool   btc_ok;
    double btc_usd, btc_change;   // USD, 24h change %
    time_t btc_time;

    // Last attempt per item: 0 = ok, else HTTP status / -1 network / -2 parse
    // (only set when every source for that item failed)
    int    gold_err, usd_err, btc_err;
    int    last_error;            // the most recent of those, 0 = none
    const char *last_error_src;   // "Vàng", "USD", "BTC"
};

typedef void (*market_cb_t)(const market_t &m);

void market_begin();
void market_set_callback(market_cb_t cb);
void market_loop(bool page_visible);  // BTC refreshes faster while the page is visible
// USD rate fetched elsewhere (the PC script, from the same Vietcombank API).
// Safe to call from any task; applied by the next market_loop().
void market_push_usd(double buy, double sell, time_t fetched_utc);
const market_t &market_get();

// ---- Parsers (plain C++, testable on a PC) ----
bool market_parse_sjc(const char *body, double &buy, double &sell);       // -> VND / luong
bool market_parse_sjc_ring(const char *body, double &buy, double &sell);  // -> VND / luong
bool market_parse_vnappmob(const char *body, double &buy, double &sell);  // -> VND / luong
bool market_parse_vnappmob_ring(const char *body, double &buy, double &sell);
bool market_parse_vnappmob_key(const char *body, char *key, size_t n);
bool market_parse_vcb(const char *body, double &buy, double &sell);
bool market_parse_binance(const char *body, double &price, double &change);
bool market_parse_coingecko(const char *body, double &price, double &change);
