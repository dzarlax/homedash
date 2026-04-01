#pragma once

// --- Firmware version (bump before each release) ---
#define FW_VERSION  "1.0.1"

// --- Location ---
#define WEATHER_CITY "Belgrade"

// --- Timing ---
#define BRIDGE_UPDATE_INTERVAL_MS  (30 * 1000)   // 30 seconds
#define OTA_CHECK_INTERVAL_MS      (30 * 60 * 1000)  // 30 minutes

// --- NTP ---
#define NTP_SERVER  "pool.ntp.org"
#define POSIX_TZ    "CET-1CEST,M3.5.0,M10.5.0/3"

// All secrets (WiFi, Bridge URL/key) are loaded from NVS at runtime.
// See tools/gen_nvs.py and README for initial setup.
