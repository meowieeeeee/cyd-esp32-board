#pragma once
#include <lvgl.h>
#include <time.h>
#include "../market/market.h"
#include "../fuel/fuel.h"

// "Giá cả" page: own header (title + segmented tabs), two tabs:
//   Vàng · Tỷ giá - SJC bar + ring 99.99 (million VND / lượng), USD/VND (Vietcombank),
//                   Bitcoin; buy / sell + the change at the last adjustment
//   Xăng          - Petrolimex zone-1 table with the change vs the previous period
// Tabs are switched by main.cpp as steps of the normal left/right paging.

enum { MARKET_TAB_GOLD = 0, MARKET_TAB_FUEL, MARKET_TAB_N };

lv_obj_t *market_page_create(lv_obj_t *parent);
void market_page_update(const market_t &m, int tz_offset_sec);
void market_page_update_fuel(const fuel_t &f, time_t now_utc, int tz_offset_sec);
void market_page_set_tab(int tab);
int  market_page_tab();
