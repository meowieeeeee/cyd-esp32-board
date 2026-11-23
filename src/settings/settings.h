#pragma once
#include <stdint.h>
#include <stdbool.h>

// Settings page at http://<board IP>/settings (http://clawd.local/ redirects
// here): Claude token, weather location, screen, pages, events. Saved in NVS,
// applied immediately (the token is stored by claude_token_store).

// Never renumber: ids are stored in NVS. New pages get new ids at the end.
enum page_id_t {
    PG_QUOTA = 0, PG_PC, PG_WEATHER, PG_LUNAR, PG_TET, PG_MARKET, PG_EVENTS, PG_FUEL, PG_COUNT
};

struct settings_t {
    float   lat, lon;            // weather location
    char    city[40];            // label only (UTF-8)
    uint8_t sleep_min;           // screen off after N min without PC data, 0 = never
    uint8_t brightness;          // 10..100 %
    uint8_t tet_days;            // show the Tet countdown page N days before Tet, 0 = never
    uint8_t rotate_sec;          // auto page flip every N seconds, 0 = off
    uint8_t order[PG_COUNT];     // page ids in display order
    uint16_t enabled;            // bit (1 << page_id)
};

typedef void (*settings_cb_t)();

void settings_begin();                         // load from NVS; call before using settings_get()
void settings_register_routes();               // /settings + /touchcal (after wifi_manager_begin)
const settings_t &settings_get();
void settings_set_changed_callback(settings_cb_t cb);   // called after a save
void settings_set_touchcal_callback(settings_cb_t cb);  // "calibrate touch" button
void settings_set_token_callback(settings_cb_t cb);     // a new Claude token was saved
const char *settings_page_name(int page_id);   // UTF-8, for the web page
