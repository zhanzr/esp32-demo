# ESP32-P4 CB V3.2 — wifi_con_test

WiFi connection test, behavior-identical to `c3-classic/wifi_con_test`:

1. Scan and list up to 20 APs (SSID, RSSI, channel, auth mode)
2. Connect to `DEFAULT_AP` with WPA2 (30 s timeout), fall back to WPA/WPA2
   mixed (20 s)
3. Print IP/gateway/netmask on success
4. Heartbeat-blink **LED0 (GPIO 14, green, low active)** at 500 ms and log a
   `WiFi status: UP/DOWN` line (with RSSI + IP) every 30 s

## P4-specific: WiFi runs on the C6

The ESP32-P4 has no radio. WiFi is provided by the on-board **ESP32-C6
co-module** through **esp_hosted** (SDIO, slot 1 = GPIO 49–54, 4-bit @ 40 MHz,
reset via C6_EN GPIO 19) + `esp_wifi_remote`, which expose the normal
`esp_wifi_*` API — the app code is unchanged from the C3 version.

**Prerequisite:** the C6 must be flashed with the esp-hosted slave
(`esp_hosted_ng` network slave) firmware. If `esp_wifi_init` fails or the
console shows SDIO transport errors, flash the C6 slave first (see the vendor
docs in `main-esp32p4-cb-32`, example `18_wifi-camera`).

Credentials go in `main/wifi_config.h` (copy from `wifi_config.h.example`;
gitignored).
