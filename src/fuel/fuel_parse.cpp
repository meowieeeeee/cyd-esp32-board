// Fuel price JSON parsing without a JSON library. Plain C++ (no Arduino), so it
// can be tested on a PC.
#include "fuel.h"
#include <ctype.h>
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

// End of the JSON value (object/array/string) starting at p (string-aware).
static const char *skip_value(const char *p)
{
    if (*p == '"') {
        for (p++; *p; p++) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == '"') return p + 1;
        }
        return p;
    }
    if (*p != '{' && *p != '[') {
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
        return p;
    }
    int depth = 0;
    bool in_str = false;
    for (; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == '"') in_str = false;
        } else if (*p == '"') in_str = true;
        else if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            if (--depth == 0) return p + 1;
        }
    }
    return p;
}

// Calls fn(start, end) for every object in the array under `key`.
template <typename F>
static int each_object(const char *body, const char *key, F fn)
{
    const char *v = find_key(body, body + strlen(body), key);
    if (!v || *v != '[') return 0;
    const char *p = v + 1;
    int n = 0;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p != '{') break;
        const char *e = skip_value(p);
        fn(p, e);
        n++;
        p = e;
    }
    return n;
}

static bool read_str(const char *v, char *out, size_t n)
{
    if (!v || *v != '"') return false;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i < n - 1) {
        if (*v == '\\' && v[1]) { out[i++] = *v++; if (i >= n - 1) break; }
        out[i++] = *v++;
    }
    out[i] = 0;
    return true;
}

// Whole VND: 19100, 19100.0, "19100", "19.100", "19,100"
static long read_price(const char *v)
{
    if (!v) return 0;
    if (*v == '"') {
        long x = 0;
        bool any = false;
        for (v++; *v && *v != '"'; v++) {
            if (isdigit((unsigned char)*v)) { x = x * 10 + (*v - '0'); any = true; }
            else if (*v != '.' && *v != ',' && *v != ' ') break;
        }
        return any ? x : 0;
    }
    char *e;
    double d = strtod(v, &e);
    return e == v ? 0 : (long)(d + 0.5);
}

// Upper-case ASCII, spaces removed: "Xăng E10 RON 95 - III" -> "X?NGE10RON95-III"
static void normalize(const char *in, char *out, size_t n)
{
    size_t i = 0;
    for (; *in && i < n - 1; in++) {
        if (*in == ' ') continue;
        out[i++] = (char)toupper((unsigned char)*in);
    }
    out[i] = 0;
}

const char *const FUEL_NAMES[FUEL_N] = {
    "E10 RON 95-III", "E10 RON 95-V", "E5 RON 92-II", "DO 0,05S-II", "DO 0,001S-V",
};

static int classify(const char *name)
{
    char s[96];
    normalize(name, s, sizeof(s));
    const char *p95 = strstr(s, "95-");
    if (p95) {
        if (strncmp(p95, "95-III", 6) == 0) return FUEL_RON95_3;
        if (strncmp(p95, "95-V", 4) == 0 && p95[4] != 'I') return FUEL_RON95_5;
        return -1;                                   // 95-IV etc.
    }
    if (strstr(s, "E5") && strstr(s, "92")) return FUEL_E5;
    if (strstr(s, "0,001S") || strstr(s, "0.001S")) return FUEL_DO_001;
    if (strstr(s, "0,05S") || strstr(s, "0.05S")) return FUEL_DO_05;
    return -1;
}

static bool plausible(long p) { return p >= 5000 && p <= 100000; }

// "2026-11-19T15:00:00+07:00" -> "2026-11-19T15:00"; "2026-11-19" stays
static void copy_date(const char *v, char *out, size_t n)
{
    char tmp[40];
    if (!read_str(v, tmp, sizeof(tmp))) return;
    if (strlen(tmp) < 10 || tmp[4] != '-' || tmp[7] != '-') return;
    size_t len = (strlen(tmp) >= 16 && tmp[10] == 'T') ? 16 : 10;
    if (len >= n) len = n - 1;
    memcpy(out, tmp, len);
    out[len] = 0;
}

static bool generic(const char *body, const char *array_key, const char *name_key,
                    const char *price_key, const char *date_key,
                    long price[FUEL_N], char *eff, size_t n)
{
    for (int i = 0; i < FUEL_N; i++) price[i] = 0;
    if (eff && n) eff[0] = 0;
    each_object(body, array_key, [&](const char *s, const char *e) {
        char name[96];
        if (!read_str(find_key(s, e, name_key), name, sizeof(name))) return;
        int k = classify(name);
        if (k < 0 || price[k]) return;           // first match per product
        long p = read_price(find_key(s, e, price_key));
        if (!plausible(p)) return;
        price[k] = p;
        if (date_key && eff) {                   // newest date among the products
            char d[20] = "";
            copy_date(find_key(s, e, date_key), d, sizeof(d));
            if (d[0] && strcmp(d, eff) > 0) strncpy(eff, d, n - 1), eff[n - 1] = 0;
        }
    });
    int found = 0;
    for (int i = 0; i < FUEL_N; i++) found += price[i] > 0;
    return found >= 2;   // tolerate one product being renamed
}

bool fuel_parse_petrolimex(const char *body, long price[FUEL_N], char *eff, size_t n)
{
    // {"Objects":[{"Title":"Xăng E10 RON 95-III","Zone1Price":19100,"Zone2Price":19480,
    //   "LastModified":"2026-11-19T15:00:00",...}, ...]}
    return generic(body, "Objects", "Title", "Zone1Price", "LastModified", price, eff, n);
}

bool fuel_parse_vietfuel(const char *body, long price[FUEL_N], char *eff, size_t n)
{
    // {"meta":{"priceDate":"2026-11-19",...},"data":[{"name":"Xăng E10 RON 95-III",
    //   "region1":19100,"region2":19480,"unit":"VND/lít"}, ...]}
    bool ok = generic(body, "data", "name", "region1", nullptr, price, eff, n);
    if (ok && eff && n) copy_date(find_key(body, body + strlen(body), "priceDate"), eff, n);
    return ok;
}
