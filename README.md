# HomeDash

A smart home dashboard for the **Waveshare ESP32-S3-Touch-LCD-7B** (7" 1024×600 touchscreen). All data flows through a single [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) server — the firmware contains **zero secrets** and can be published publicly.

## Features

- **3-page tileview** — swipe between pages:
  - **Page 1**: Weather, HA Calendar events, real-time bus arrivals
  - **Page 2**: Health metrics (readiness, steps, sleep, HR, HRV, SpO2), Todoist tasks, news
  - **Page 3**: Home Assistant light controls + air quality sensors per room
- **OTA updates** — firmware updates over WiFi via Bridge, with automatic rollback
- **Full Cyrillic UI** — Russian-localized interface with custom Montserrat fonts
- **Dual-core** — LVGL renders on Core 1, all network I/O on Core 0
- **Anti-tear display** — PSRAM double-buffer + bounce buffer pipeline

## Hardware

| Component | Details |
|-----------|---------|
| Board | Waveshare ESP32-S3-Touch-LCD-7B |
| CPU | ESP32-S3 @ 240 MHz |
| RAM | 8 MB OPI PSRAM @ 120 MHz |
| Flash | 16 MB QIO @ 120 MHz |
| Display | 7" 1024×600 RGB565, ST7262 controller |
| Touch | GT911 capacitive (I2C @ 0x5D) |

## Architecture

```
ESP32 Bridge (Go server)
  ├─ /api/dashboard    → health, tasks, news, sensors, lights, weather, transport
  ├─ /api/calendar     → on-demand HA calendar events
  ├─ /api/ha/action    → light toggle (returns fresh state)
  ├─ /api/ota/check    → firmware version comparison
  └─ /api/ota/firmware → streams firmware binary for OTA
         ↕
    ESP32 Display
      NVS: wifi_ssid, wifi_pass, bridge_url, bridge_key
      Polls /api/dashboard every 30s
      Checks OTA every 30 min (or manual button on Page 3)
```

All secrets (WiFi credentials, Bridge URL, API key) are stored in the **NVS partition**, not in the firmware binary.

### Dual-Core Layout

| Core | Task | Responsibility |
|------|------|----------------|
| Core 1 | `lvgl_port_task` | LVGL render loop @ 60 FPS, touch input |
| Core 0 | `network_task` | WiFi, NTP, Bridge polling, OTA, on-demand calendar/light requests |

### Partition Table

```
nvs       24KB   — NVS config (WiFi, Bridge credentials)
otadata    8KB   — OTA slot tracker
phy_init   4KB   — radio calibration
ota_0      7MB   — firmware slot A
ota_1      7MB   — firmware slot B
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP-IDF 5.3+ (managed automatically by PlatformIO)
- Python 3 (for NVS binary generation)
- A running [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) instance

### First-Time Setup

1. **Build and flash firmware** (USB required once):
   ```bash
   pio run -t upload
   ```

2. **Generate NVS binary** with your credentials:
   ```bash
   python3 tools/gen_nvs.py
   ```
   You'll be prompted for:
   - WiFi SSID and password
   - Bridge URL (e.g. `https://esp32-bridge.example.com`)
   - Bridge API key

3. **Flash NVS** to the device:
   ```bash
   esptool.py --port /dev/cu.usbmodemXXX write_flash 0x9000 nvs.bin
   ```

4. **Done.** All future updates are delivered via OTA.

### Releasing a New Version

1. Bump `FW_VERSION` in `src/config.h`
2. Commit, push, and create a tag:
   ```bash
   git tag v1.0.1
   git push --tags
   ```
3. GitHub Actions builds firmware and creates a Release with `firmware.bin`
4. Update Bridge `.env`:
   ```
   OTA_FIRMWARE_VERSION=1.0.1
   OTA_FIRMWARE_URL=https://github.com/dzarlax/homedash/releases/download/v1.0.1/firmware.bin
   ```
5. Restart Bridge — the display picks up the update within 30 minutes (or tap "Обновить" on Page 3)

### OTA Rollback

If the new firmware fails to connect to Bridge after boot, the bootloader automatically reverts to the previous working version (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | App entry, network task, cross-core coordination |
| `src/ui_dashboard.cpp/.h` | All UI creation and update logic (3 pages) |
| `src/bridge.cpp/.h` | Bridge API client — dashboard, calendar, light toggle |
| `src/ota.cpp/.h` | OTA update module (check + download via Bridge) |
| `src/nvs_config.cpp/.h` | NVS credential reader |
| `src/weather_icons.cpp/.h` | Canvas-drawn weather icons |
| `src/wifi_manager.cpp/.h` | Event-driven WiFi connect/reconnect |
| `src/config.h` | Compile-time constants (version, NTP, timezone) |
| `tools/gen_nvs.py` | NVS binary generator |
| `partitions.csv` | Dual OTA partition layout |

## Custom Fonts

Cyrillic support uses fonts generated with [`lv_font_conv`](https://github.com/lvgl/lv_font_conv):

```bash
lv_font_conv --font Montserrat-Medium.ttf \
  --range 0x400-0x4FF \
  --size 16 --bpp 4 \
  --no-compress --no-prefilter \
  --format lvgl \
  --lv-fallback lv_font_montserrat_16 \
  -o src/fonts/font_montserrat_16_cyr.c
```

## Related

- [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) — Go backend that aggregates all data sources and proxies OTA updates

## License

MIT
