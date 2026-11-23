#pragma once
#include <stdint.h>
#include <stdbool.h>

// PC stats pushed from the computer by tools/pc_monitor.py.
//
// The board does not poll the PC - the PC sends to the board's web server
// (the same one that serves the config page):
//
//   POST http://clawd.local/pc        (form fields, all optional)
//     host       computer name
//     cpu        CPU load %            cpu_temp   CPU temperature °C
//     gpu        GPU load %            gpu_temp   GPU temperature °C
//     ram_used   RAM used, GB          ram_total  RAM total, GB
//     vram_used  GPU memory used, GB   vram_total GPU memory total, GB
//     cpu_name   "Ryzen 7 5700X"       gpu_name   "RTX 3060"   (kept until changed)
//     cpu_clock  CPU clock, MHz        gpu_clock  GPU core clock, MHz
//     cpu_power  CPU package power, W  gpu_power  GPU power, W
//     mac        MAC of the PC's network card (for Wake-on-LAN, see wol/wol.h)
//     nic        "eth" or "wifi"
//     usd_buy, usd_sell, usd_t   USD/VND from Vietcombank's JSON API (transfer buy / sell)
//                and when it was fetched (unix time) -> prices page, see market_push_usd()
//
// Quick test without the script:
//   curl "http://clawd.local/pc?cpu=42&cpu_temp=61&ram_used=12.4&ram_total=32"
//
// A field that was not sent is NAN -> the screen shows "--".

#define PC_STALE_MS 6000UL // no data for this long -> "waiting for PC"

struct pc_stats_t {
    bool     valid;        // received at least once since boot
    uint32_t rx_ms;        // millis() of the last update

    float cpu, cpu_temp;
    float gpu, gpu_temp;
    float ram_used, ram_total;
    float vram_used, vram_total;
    float cpu_clock, gpu_clock;   // MHz
    float cpu_power, gpu_power;   // W

    char cpu_name[24], gpu_name[24];   // "" = not sent yet

    char host[32];
};

typedef void (*pc_stats_cb_t)(const pc_stats_t &s);

void pc_stats_begin();                         // registers the /pc route; call after wifi_manager_begin()
bool pc_stats_fresh();                         // data received within PC_STALE_MS
const pc_stats_t &pc_stats_get();
void pc_stats_set_callback(pc_stats_cb_t cb);  // called on every update
