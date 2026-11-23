#pragma once
#include <Arduino.h>

// Input from the XPT2046 touch panel and the BOOT button (GPIO0):
//   - tap  -> TOUCH_TAP (fires on release); touch_tap_side() says whether it was
//             on the left or right half of the screen (BOOT button: side 0)
//   - hold for TOUCH_LONG_MS -> TOUCH_LONG (fires once while still held)
//
// Left / right needs a 2-tap calibration (right edge, then left edge), saved in
// NVS. It tells which of the two raw channels runs along the screen's width and
// in which direction, so it works whatever the panel/rotation variant is.
//
// CYD touch wiring (separate SPI bus from the display):
//   CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36

#define TOUCH_LONG_MS 1000

enum touch_event_t { TOUCH_NONE = 0, TOUCH_TAP, TOUCH_LONG };

void touch_init();                 // also loads the calibration
touch_event_t touch_poll();        // call every loop()
bool touch_is_down();              // screen or BOOT currently pressed (debounced)
uint32_t touch_held_ms();          // how long the current press has lasted; 0 if not pressed
                                   // (or once TOUCH_LONG has fired for this press)

// Last TAP: averaged raw readings of the two channels; false if it came from
// the BOOT button.
bool touch_tap_raw(int &ch_a, int &ch_b);
int  touch_tap_side();             // -1 left, +1 right, 0 = BOOT / unknown

// Calibration
bool touch_cal_valid();
void touch_cal_save(int axis, int mid, bool right_high); // axis 0 = ch_a, 1 = ch_b
