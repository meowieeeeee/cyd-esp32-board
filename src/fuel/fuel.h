#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Petrolimex retail fuel prices, zone 1 (vùng 1: HCMC, Hanoi, ...), VND/litre:
//   RON95_3 = "Xăng E10 RON 95-III" (older name "Xăng RON 95-III")
//   RON95_5 = "Xăng E10 RON 95-V"
//   E5      = "Xăng E5 RON 92-II"
//   DO_05   = "DO 0,05S-II"  / "Điêzen 0,05S-II"
//   DO_001  = "DO 0,001S-V"  / "Điêzen 0,001S-V"
//
// Sources: Petrolimex's own CMS API (portals.petrolimex.com.vn), fallback
// vietfuel-api (aggregator). Polled every 20 min.
//
// Adjustment detection: the last prices are kept in NVS. When a fetch returns
// different prices (and an effective date not older than the stored one), the
// differences become the "deltas" and `just_changed` is true for that update.

enum { FUEL_RON95_3 = 0, FUEL_RON95_5, FUEL_E5, FUEL_DO_05, FUEL_DO_001, FUEL_N };

// Short names as Petrolimex prints them (for the screen)
extern const char *const FUEL_NAMES[FUEL_N];

struct fuel_t {
    bool   ok;                 // have prices
    long   price[FUEL_N];      // VND / litre, zone 1, 0 = unknown
    long   delta[FUEL_N];      // change vs the previous period (0 = same / unknown)
    bool   has_delta;          // at least one adjustment seen since first run
    char   effective[20];      // "2026-11-19T15:00" (local time as published) or ""
    time_t changed_at;         // UTC epoch when the last adjustment was detected, 0 = never
    bool   just_changed;       // this update detected a new adjustment
    time_t fetched;            // UTC epoch of the last successful fetch
    const char *src;           // "Petrolimex" / "VietFuel"
    int    last_error;         // HTTP status / -1 network / -2 parse, 0 = ok
};

typedef void (*fuel_cb_t)(const fuel_t &f);

void fuel_begin();                 // loads the stored prices (NVS)
void fuel_set_callback(fuel_cb_t cb);
void fuel_loop();                  // call from the network task
const fuel_t &fuel_get();

// ---- Parsers (plain C++, testable on a PC) ----
// Fill price[] (0 when a product is missing) and effective ("YYYY-MM-DDTHH:MM" or "YYYY-MM-DD").
bool fuel_parse_petrolimex(const char *body, long price[FUEL_N], char *effective, size_t n);
bool fuel_parse_vietfuel(const char *body, long price[FUEL_N], char *effective, size_t n);
