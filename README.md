# HomeDash

A smart home dashboard for the **Waveshare ESP32-S3-Touch-LCD-7B** (7" 1024×600 touchscreen). Displays weather, Home Assistant calendar events, and real-time public transport arrivals — all on a single glanceable screen.


## Features

- **Clock & date** — synced via NTP, full Cyrillic support
- **Weather** — current conditions + tomorrow forecast via [Open-Meteo](https://open-meteo.com/), canvas-drawn icons (no image assets)
- **HA Calendar** — events from Home Assistant, color-coded by source calendar, tap any day for on-demand fetch
- **Transport** — real-time bus arrivals for two stops, color-coded by proximity (green → red)
- **Anti-tear display** — PSRAM double-buffer + bounce buffer pipeline at 1024×600 RGB565
- **Dual-core** — LVGL renders on Core 1, all network I/O on Core 0

## Hardware

| Component | Details |
|-----------|---------|
| Board | Waveshare ESP32-S3-Touch-LCD-7B |
| CPU | ESP32-S3 @ 240 MHz |
| RAM | 8 MB OPI PSRAM @ 120 MHz |
| Flash | 16 MB QIO @ 120 MHz |
| Display | 7" 1024×600 RGB565, ST7262 controller |
| Touch | GT911 capacitive (I2C @ 0x5D) |
| IO expander | CH422G (I2C @ 0x24) |

## UI Layout

```
┌─────────────────────────────────────────────────────┐
│  DateTime                           City + Temp     │  60px
├──────────────┬──────────────────────────────────────┤
│ [icon]Weather│ Schedule title            [Today btn]│
│  H:% W: Tmrw│ event 1 (colored by calendar)        │
│──────────────│ event 2 ...                          │
│              │ (up to 8 events, 35px each)          │
│  Calendar    ├──────────────────────────────────────┤
│  410×510     │ Djeram                               │
│              │ → Oblast  5:3min  7L:8min  14:12min  │
│              │ ← Centar  6:10min  7L:15min          │
├──────────────┴──────────────────────────────────────┤
│  WiFi  Heap  Weather/Cal/Transport status           │  20px
└─────────────────────────────────────────────────────┘
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP-IDF 5.3 (managed automatically by PlatformIO)

### Configuration

1. Copy the example config and fill in your credentials:
   ```bash
   cp src/config.h.example src/config.h
   ```

2. Edit `src/config.h`:
   ```c
   #define WIFI_SSID       "your_network"
   #define WIFI_PASSWORD   "your_password"

   #define WEATHER_LAT     44.82          // your latitude
   #define WEATHER_LON     20.46          // your longitude
   #define WEATHER_CITY    "Belgrade"     // display name

   #define HA_BASE_URL     "http://192.168.1.100:8123"
   #define HA_TOKEN        "your_long_lived_access_token"

   #define NTP_SERVER      "pool.ntp.org"
   #define POSIX_TZ        "CET-1CEST,M3.5.0,M10.5.0/3"  // your timezone
   ```

   > **HA token**: in Home Assistant go to *Profile → Long-Lived Access Tokens → Create Token*.

3. Adjust your timezone string. Examples:
   - Belgrade / Paris: `CET-1CEST,M3.5.0,M10.5.0/3`
   - Moscow: `MSK-3`
   - UTC: `UTC0`

### Build & Flash

```bash
pio run                  # Build only
pio run -t upload        # Build + flash
pio device monitor       # Serial monitor (115200 baud)
```

> **Windows note**: VS Code PlatformIO serial monitor holds the COM port. Either use the Upload button in VS Code, or close the monitor before running `pio run -t upload`. The board may need boot mode — hold **BOOT** + press **RST** before flashing.

### After changing sdkconfig

Delete generated files to force regeneration:
```bash
rm sdkconfig.esp32-s3-touch-lcd-7b
rm -rf .pio/
pio run
```

## Architecture

### Dual-Core Task Layout

| Core | Task | Responsibility |
|------|------|----------------|
| Core 1 | `lvgl_port_task` | LVGL render loop, touch input |
| Core 0 | `network_task` | WiFi, NTP, weather (30 min), calendar (5 min), transport (1 min) |

All LVGL calls must be wrapped in `lvgl_port_lock()` / `lvgl_port_unlock()`.

### Cross-Core Communication

Tapping a calendar day on Core 1 triggers an on-demand fetch on Core 0 via `xTaskNotifyGive()`. The network task wakes instantly on notification or after a 10 s timeout for scheduled polling.

### Display Pipeline

PSRAM framebuffers → bounce buffers (PSRAM → internal SRAM → LCD DMA). Anti-tear mode 3: double-buffer + direct mode. The `flush_callback` blocks on `ulTaskNotifyTake()` until the bounce DMA ISR fires.

### PSRAM XIP

`sdkconfig.defaults` enables `SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA` + `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, moving code, rodata, and WiFi/LWIP buffers to PSRAM. This prevents WiFi DMA from competing with LCD DMA for internal SRAM ("jumping columns" artifact).

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | App entry, network task, cross-core coordination |
| `src/ui_dashboard.cpp/.h` | All UI creation and update logic |
| `src/ha_calendar.cpp/.h` | Home Assistant calendar API |
| `src/transport.cpp/.h` | Transport API (stops 89/90), sort by arrival |
| `src/weather.cpp/.h` | Open-Meteo API fetch and parsing |
| `src/weather_icons.cpp/.h` | Canvas-drawn weather icons |
| `src/wifi_manager.cpp/.h` | Event-driven WiFi connect/reconnect |
| `src/lvgl_port.cpp/.h` | LVGL FreeRTOS integration, flush callback |
| `src/rgb_lcd_port.cpp/.h` | RGB LCD panel init, bounce buffers |
| `src/gt911.cpp/.h` | GT911 touch I2C driver |
| `src/config.h.example` | Configuration template (copy → `config.h`) |

## Custom Fonts

Cyrillic support uses a font generated with [`lv_font_conv`](https://github.com/lvgl/lv_font_conv):

```bash
lv_font_conv --font Montserrat-Medium.ttf \
  --range 0x400-0x4FF \
  --size 16 --bpp 4 \
  --no-compress --no-prefilter \
  --format lvgl \
  -o src/fonts/font_montserrat_16_cyr.c
```

> `--no-compress --no-prefilter` is required — `LV_USE_FONT_COMPRESSED` is disabled in `lv_conf.h`.

## License

MIT
