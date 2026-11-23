#pragma once
#include <stdint.h>
#include <stdbool.h>

// Wake-on-LAN: sends a "magic packet" (6x 0xFF + 16x the PC's MAC) as UDP
// broadcast on port 9.
//
// The MAC is learned automatically: tools/pc_monitor.py sends the MAC of the
// network card it uses (field "mac" on /pc). It is saved in NVS so waking
// still works after the board reboots while the PC is off.
//
// Also available over HTTP (handy from a phone):
//   http://clawd.local/wol                      -> wake the saved PC
//   http://clawd.local/wol?mac=AA:BB:CC:DD:EE:FF -> save this MAC, then wake

void wol_begin();                   // loads the saved MAC, registers /wol (after wifi_manager_begin)
bool wol_has_mac();
const char *wol_mac_str();          // "AA:BB:CC:DD:EE:FF" or ""
bool wol_set_mac(const char *mac);  // accepts ':' '-' or no separators; saves only if changed
bool wol_send();                    // false if no MAC / no WiFi
uint32_t wol_last_sent_ms();        // millis() of the last send, 0 = never

// Whether the PC reported its network card as Wi-Fi (WoL usually needs Ethernet).
void wol_set_nic_wifi(bool wifi);
bool wol_nic_wifi();
