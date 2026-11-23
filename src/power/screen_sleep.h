#pragma once
#include <stdint.h>
#include <stdbool.h>

// Screen sleep: the backlight fades out when the PC has sent nothing for a
// while (PC off / asleep), and comes back when the PC sends data again or
// when the screen / BOOT is touched.
//
//   awake while:  PC data within sleep_after_ms
//            or:  touched within touch_awake_ms
//
// The touch that wakes the screen is "eaten": it doesn't switch the page or
// trigger Wake-on-LAN.

void screen_sleep_begin(uint32_t sleep_after_ms, uint32_t touch_awake_ms); // 0 = never sleep
void screen_sleep_loop();          // call every loop(): timers + backlight fade
void screen_sleep_pc_activity();   // call when PC data arrives (wakes the screen)
bool screen_sleep_touch();         // call while the screen is touched; true = this touch
                                   // woke the screen, so ignore it
bool screen_sleep_is_asleep();     // dark or fading out
