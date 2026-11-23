#pragma once
#include <lvgl.h>
#include "../pc_stats/pc_stats.h"

// "PC" page. PC on: CPU gauge (load % + temperature) on the left, GPU / VRAM /
// RAM cards on the right. PC off: the Wake-on-LAN panel (see wol_page.h)
// takes the same area. Status line at the bottom.

lv_obj_t *pc_page_create(lv_obj_t *parent);        // returns the full-screen page container
void pc_page_update(const pc_stats_t &s, bool fresh); // fresh=false -> shows "--"
void pc_page_set_status(const char *msg, bool accent); // bottom line; accent=orange, else dim
void pc_page_show_wol(bool show);                      // true = Wake-on-LAN panel instead of stats
