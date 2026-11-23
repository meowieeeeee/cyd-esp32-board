#pragma once
#include <Arduino.h>

// Forward declaration only: including <WebServer.h> here breaks main.cpp,
// because TFT_eSPI includes FS.h with FS_NO_GLOBALS (no global "FS" type)
// and WebServer.h needs it. Include <WebServer.h> in the .cpp that uses it.
class WebServer;

// WiFi connection + config page (captive portal).
//
// Flow:
//   1. On boot, read the saved SSID/password from NVS and try to connect (STA).
//   2. If none saved or connection fails -> board starts its own WiFi (AP) +
//      web server. User joins the board's WiFi, opens its IP, enters the
//      home WiFi name + password -> board saves to NVS and restarts.
//   3. Next boot connects directly. On the LAN, http://clawd.local/ redirects
//      to /settings (Claude token, pages...); /wifi changes the network.
//
// Hold the BOOT button (GPIO0) while powering on to wipe the config and
// go back to the config page.

typedef void (*wifi_status_cb_t)(const char *msg);

// Call once in setup(). ap_ssid: WiFi name the board broadcasts when it needs
// configuring. ap_pass: >= 8 chars gives the AP a password, otherwise it's open.
// This function CAN block for up to ~15s while trying the saved WiFi.
void wifi_manager_begin(const char *ap_ssid, const char *ap_pass = nullptr);

// Call every loop() to serve the web server + DNS while in config mode.
void wifi_manager_loop();

// true once connected to WiFi (STA).
bool wifi_manager_connected();

// true if a WiFi SSID is saved in NVS, regardless of current connection state.
bool wifi_manager_has_credentials();

// Registers a callback for status updates (shown on screen).
void wifi_manager_set_status_callback(wifi_status_cb_t cb);

// Clears the saved SSID/password.
void wifi_manager_reset();

// The board's web server (port 80), so other modules can add routes
// (e.g. pc_stats registers /pc). Served from wifi_manager_loop().
WebServer &wifi_manager_server();
