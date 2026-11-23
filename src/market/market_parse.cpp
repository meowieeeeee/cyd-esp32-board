// Small JSON readers for the price sources (no JSON library needed: the values
// we want are flat key/value pairs). Plain C++ so they can be tested on a PC.
#include "market.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pointer just after `"key":` within [p, end), or nullptr.
static const char *find_key(const char *p, const char *end, const char *key)
{
    size_t klen = strlen(key);
    while (p && p < end) {
        const char *q = strstr(p, key);
        if (!q || q >= end) return nullptr;
        if (q > p && q[-1] == '"' && q[klen] == '"') {
            const char *c = q + klen + 1;
            while (*c == ' ') c++;
            if (*c == ':') {
                c++;
                while (*c == ' ') c++;
                return c;
            }
        }
        p = q + klen;
    }
    return nullptr;
}

// Number, possibly quoted and with thousands commas: 119000000.0, "119,000", "26130.00"
static bool read_num(const char *v, double &out)
{
    if (!v) return false;
    if (*v == '"') v++;
    char buf[32];
    size_t n = 0;
    for (; *v && n < sizeof(buf) - 1; v++) {
        if (isdigit((unsigned char)*v) || *v == '.' || *v == '-' || *v == '+' || *v == 'e' || *v == 'E')
            buf[n++] = *v;
        else if (*v == ',')
            continue; // thousands separator
        else
            break;
    }
    buf[n] = 0;
    if (!n) return false;
    char *e;
    out = strtod(buf, &e);
    return e != buf;
}

static bool read_str(const char *v, char *out, size_t n)
{
    if (!v || *v != '"') return false;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i < n - 1) out[i++] = *v++;
    out[i] = 0;
    return true;
}

static bool num_key(const char *p, const char *end, const char *key, double &out)
{
    return read_num(find_key(p, end, key), out);
}

// Bounds of the flat {...} object around `pos`.
static void object_around(const char *body, const char *pos, const char *&start, const char *&end)
{
    start = pos;
    while (start > body && *start != '{') start--;
    end = strchr(pos, '}');
    if (!end) end = pos + strlen(pos);
}

static double to_vnd_per_luong(double v)
{
    return v < 1e6 ? v * 1000.0 : v; // SJC quotes in thousand VND / luong
}

static bool plausible_gold(double v) { return v >= 1e6 && v <= 1e10; }

// Buy/sell of the flat object around `pos` (SJC row), in VND / luong.
static bool sjc_row(const char *body, const char *pos, double &buy, double &sell)
{
    const char *s, *e;
    object_around(body, pos, s, e);
    double b, sl;
    if (!num_key(s, e, "BuyValue", b) && !num_key(s, e, "Buy", b)) return false;
    if (!num_key(s, e, "SellValue", sl) && !num_key(s, e, "Sell", sl)) return false;
    b = to_vnd_per_luong(b);
    sl = to_vnd_per_luong(sl);
    if (!plausible_gold(b) || !plausible_gold(sl)) return false;
    buy = b;
    sell = sl;
    return true;
}

// "nhẫn" as raw UTF-8 or as a JSON \u escape
static bool is_ring(const char *name)
{
    return strstr(name, "nh\xE1\xBA\xABn") || strstr(name, "Nh\xE1\xBA\xABn") ||
           strstr(name, "nh\\u1eabn") || strstr(name, "Nh\\u1eabn") ||
           strstr(name, "nh\\u1EABn") || strstr(name, "Nh\\u1EABn");
}

bool market_parse_sjc(const char *body, double &buy, double &sell)
{
    // {"success":true,"data":[{"TypeName":"Vàng SJC 1L, 10L, 1KG","BranchName":"Hồ Chí Minh",
    //   "Buy":"119,000","BuyValue":119000000.0,"Sell":"121,000","SellValue":121000000.0}, ...]}
    const char *end_all = body + strlen(body);
    const char *fallback = nullptr;
    const char *p = body;
    while ((p = find_key(p, end_all, "TypeName")) != nullptr) {
        char name[96];
        if (read_str(p, name, sizeof(name)) && strstr(name, "SJC")) {
            if (strstr(name, "1L")) { fallback = p; break; } // the 1-luong bar row
            if (!fallback && !strstr(name, "nh")) fallback = p; // not a ring ("nhẫn")
        }
        p++;
    }
    return fallback && sjc_row(body, fallback, buy, sell);
}

bool market_parse_sjc_ring(const char *body, double &buy, double &sell)
{
    // first ring row: "Vàng nhẫn SJC 99,99% 1 chỉ, 2 chỉ, 5 chỉ"
    const char *end_all = body + strlen(body);
    const char *p = body;
    while ((p = find_key(p, end_all, "TypeName")) != nullptr) {
        char name[96];
        if (read_str(p, name, sizeof(name)) && is_ring(name) && strstr(name, "99"))
            return sjc_row(body, p, buy, sell);
        p++;
    }
    return false;
}

static bool vnappmob_pair(const char *body, const char *kb, const char *ks, double &buy, double &sell)
{
    const char *end = body + strlen(body);
    double b, s;
    if (!num_key(body, end, kb, b) || !num_key(body, end, ks, s)) return false;
    b = to_vnd_per_luong(b);
    s = to_vnd_per_luong(s);
    if (!plausible_gold(b) || !plausible_gold(s)) return false;
    buy = b;
    sell = s;
    return true;
}

bool market_parse_vnappmob(const char *body, double &buy, double &sell)
{
    // {"results":[{"buy_1l":119000000.0,"sell_1l":121000000.0,"buy_nhan1c":..,"sell_nhan1c":.., ...}]}
    return vnappmob_pair(body, "buy_1l", "sell_1l", buy, sell);
}

bool market_parse_vnappmob_ring(const char *body, double &buy, double &sell)
{
    return vnappmob_pair(body, "buy_nhan1c", "sell_nhan1c", buy, sell);
}

bool market_parse_vnappmob_key(const char *body, char *key, size_t n)
{
    // {"results":"<token>"}
    const char *v = find_key(body, body + strlen(body), "results");
    return read_str(v, key, n) && key[0];
}

bool market_parse_vcb(const char *body, double &buy, double &sell)
{
    // {"Count":20,"Date":..,"Data":[{"currencyName":"US DOLLAR","currencyCode":"USD",
    //   "cash":"26100.00","transfer":"26130.00","sell":"26400.00","icon":..}, ...]}
    const char *end_all = body + strlen(body);
    const char *p = body;
    while ((p = find_key(p, end_all, "currencyCode")) != nullptr) {
        char code[8];
        if (read_str(p, code, sizeof(code)) && strcmp(code, "USD") == 0) {
            const char *s, *e;
            object_around(body, p, s, e);
            double b, sl;
            if (!num_key(s, e, "transfer", b) && !num_key(s, e, "cash", b)) return false;
            if (!num_key(s, e, "sell", sl)) return false;
            if (b < 1e4 || b > 1e5 || sl < 1e4 || sl > 1e5) return false;
            buy = b;
            sell = sl;
            return true;
        }
        p++;
    }
    return false;
}

bool market_parse_binance(const char *body, double &price, double &change)
{
    // {"symbol":"BTCUSDT","priceChangePercent":"2.351","lastPrice":"98450.12000000",...}
    const char *end = body + strlen(body);
    if (!num_key(body, end, "lastPrice", price) || price <= 0) return false;
    if (!num_key(body, end, "priceChangePercent", change)) change = 0;
    return true;
}

bool market_parse_coingecko(const char *body, double &price, double &change)
{
    // {"bitcoin":{"usd":98450,"usd_24h_change":2.351}}
    const char *end = body + strlen(body);
    if (!num_key(body, end, "usd", price) || price <= 0) return false;
    if (!num_key(body, end, "usd_24h_change", change)) change = 0;
    return true;
}
