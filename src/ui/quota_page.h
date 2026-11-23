#pragma once
#include <lvgl.h>
#include <time.h>
#include "../claude_quota/claude_quota.h"

// Claude quota page body: two ring gauges (5-hour session, 7-day week) under
// the shared header (Clawd mascot, page dots, clock, temperature).
//
//  +----------------+ +----------------+
//  |   .-------.    | |   .-------.    |   ring = % used, white dot = how much
//  |  /  37 %   \   | |  /  12 %   \   |   of the window has passed (if the
//  |  \  5 GIỜ  /   | |  \ 7 NGÀY  /   |   color runs past the dot you are
//  |   '-------'    | |   '-------'    |   using it faster than it refills)
//  | còn 2 giờ 15p  | | còn 3 ngày 4g  |   countdown to the reset
//  | Làm mới 14:30  | | Làm mới T5 9:00|   local reset time
//  +----------------+ +----------------+

// Builds the cards inside `page` (the PG_QUOTA page object). Returns the
// container holding them, so setup mode can hide it.
lv_obj_t *quota_page_build(lv_obj_t *page, int tz_offset_sec);

// New poll result. now_utc: current epoch (0 if the clock is not set yet).
void quota_page_update(const claude_quota_t &q, time_t now_utc);

// Once a minute: recount "còn ..." and move the time marker from the last
// known reset time, so the page stays current between polls.
void quota_page_tick(time_t now_utc);
