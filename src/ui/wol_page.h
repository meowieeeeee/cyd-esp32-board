#pragma once
#include <lvgl.h>
#include <stdint.h>

// Wake-on-LAN panel, shown inside the PC page while the PC is off: PC card
// (name, online/offline, MAC) on top, a big power button below. Hold the
// screen (or BOOT) for 1 s to send the magic packet; the ring fills while
// holding. No header icon / status line of its own (the PC page has them).

enum wol_state_t {
    WOL_NO_MAC,     // MAC not learned yet
    WOL_OFFLINE,    // PC off (no data from pc_monitor.py) -> hold to wake
    WOL_WAKING,     // magic packet sent, waiting for the PC to come up
    WOL_NO_REPLY,   // sent, PC didn't come up within the timeout
    WOL_ONLINE,     // pc_monitor.py is sending -> PC is on
};

struct wol_view_t {
    wol_state_t state;
    const char *host;      // PC name ("" unknown)
    const char *mac;       // "AA:BB:.." ("" unknown)
    bool nic_wifi;         // PC is on Wi-Fi (WoL usually needs Ethernet)
    uint32_t sent_ago_s;   // seconds since the packet was sent (WAKING / NO_REPLY)
    int hold_pct;          // 0..100 while the user is holding, else -1
};

lv_obj_t *wol_panel_create(lv_obj_t *parent);   // full-screen, transparent container
void wol_panel_update(const wol_view_t &v);
